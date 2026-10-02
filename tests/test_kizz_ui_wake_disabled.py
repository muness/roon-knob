"""Execute real UI defaults and semantic predicates for OFF and opt-in builds."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'm5_beta_app/main/touch_ui.cpp').read_text()

def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{': depth += 1
        if source[end] == '}': depth -= 1
        end += 1
    return source[start:end]

state = source[source.index('struct State {'):source.index('} s;') + len('} s;')]
harness = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include "kizz_semantic_layout.h"
using m5_platform_stackchan_volume_t = int;
constexpr int M5_PLATFORM_STACKCHAN_VOLUME_LOW = 0;
static kizz_semantic_context_t captured;
void kizz_semantic_set_context(const kizz_semantic_context_t *value) { captured = *value; }
bool kizz_semantic_admit_json(const char *,size_t,char *,size_t) { return true; }
'''+state+'\n'+function('bool semantic_voice_turn_active()')+'\n'+function('extern "C" bool touch_ui_semantic_admit')+r'''
int main() {
    assert(s.sound_enabled && s.voice_volume == M5_PLATFORM_STACKCHAN_VOLUME_LOW);
    assert(s.voice_diagnostics == bool(HIPHI_KIZZ_WAKE_WORD));
    if (!HIPHI_KIZZ_WAKE_WORD) assert(!strcmp(s.voice_state,"DISABLED"));
    s.voice_diagnostics = false;
    const char *states[]={"STARTING","ARMED","DISABLED","LISTENING","REVIEW","FAULT","RECOVERING"};
    for(const char *state:states) {
        std::snprintf(s.voice_state,sizeof(s.voice_state),"%s",state);
        s.voice_listening=false; s.ever_online=false;
        bool active = strcmp(state,"STARTING") && strcmp(state,"ARMED") && strcmp(state,"DISABLED");
        assert(semantic_voice_turn_active() == bool(HIPHI_KIZZ_WAKE_WORD && active));
        touch_ui_semantic_admit("{}",2,nullptr,0);
        assert(captured.voice_input == bool(HIPHI_KIZZ_WAKE_WORD));
        assert(captured.voice_active == bool(HIPHI_KIZZ_WAKE_WORD && active));
        assert(captured.review_active == bool(HIPHI_KIZZ_WAKE_WORD && !strcmp(state,"REVIEW")));
        bool recovery = !strcmp(state,"FAULT") || !strcmp(state,"RECOVERING");
        assert(captured.recovery_active == bool(HIPHI_KIZZ_WAKE_WORD && recovery));
        assert(captured.touch_input && captured.button_input && captured.has_transport);
    }
    s.voice_diagnostics=true; s.voice_listening=true;
    assert(semantic_voice_turn_active() == bool(HIPHI_KIZZ_WAKE_WORD));
    s.ever_online=true; s.online=false;
    touch_ui_semantic_admit("{}",2,nullptr,0); assert(captured.recovery_active);
}
'''
with tempfile.TemporaryDirectory(prefix='hiphi-ui-wake-') as directory:
    tmp=Path(directory)
    cpp=tmp/'ui.cpp'; cpp.write_text(harness)
    for enabled in (0,1):
        executable=tmp/f'ui-{enabled}'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-DHIPHI_M5_TARGET_ID=4',
                        f'-DHIPHI_KIZZ_WAKE_WORD={enabled}',
                        '-I'+str(ROOT/'m5_beta_app/main'),str(cpp),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)

# Independently preprocess the whole UI conditional structure. OFF must remove
# live voice polling while retaining ordinary sound preference/trigger paths.
without_includes = re.sub(r'^\s*#include[^\n]*', '', source, flags=re.MULTILINE)
for enabled in (0,1):
    result=subprocess.run(['c++','-E','-P','-x','c++','-DHIPHI_M5_TARGET_ID=4',
                           f'-DHIPHI_KIZZ_WAKE_WORD={enabled}','-'],
                          input=without_includes,text=True,capture_output=True,check=True).stdout
    for token in ('m5_platform_voice_state()', 'm5_platform_voice_copy_transcript(',
                  'm5_platform_voice_wake_probability()'):
        assert (token in result)==bool(enabled), (enabled,token)
    for token in ('m5_platform_stackchan_sound_trigger(', 'm5_platform_stackchan_sound_volume('):
        assert token in result,(enabled,token)
print('Kizz UI OFF/ON behavior passes: no disabled voice activity/capability/polling; sound controls and network recovery preserved.')
