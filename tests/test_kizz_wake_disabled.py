"""Compile-time wake exclusion must preserve the StackChan's ordinary controls."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'components/m5_platform/m5_platform.cpp').read_text()
# Preprocess the real conditional structure without requiring the SDK headers.
# The full CI firmware graph/map check owns dependency and link verification.
source = re.sub(r'^\s*#include[^\n]*', '', source, flags=re.MULTILINE)
for enabled in (0, 1):
    result = subprocess.run(
        ['c++', '-E', '-P', '-x', 'c++',
         '-DCONFIG_M5_PLATFORM_EXPECT_STACKCHAN=1',
         f'-DHIPHI_KIZZ_WAKE_WORD={enabled}', '-'],
        input=source, text=True, capture_output=True, check=True,
    )
    for token in ('kizz_wake_word_', 'esp_afe', 'voice_feed_task',
                  'M5.Mic', 's_voice_command_buffer', 'start_voice_transport()'):
        assert (token in result.stdout) == bool(enabled), (enabled, token)
    for token in ('M5StackChan.begin();', 'M5StackChan.update();',
                  'M5.Speaker.tone', 'M5StackChan.Motion.move',
                  'm5_platform_stackchan_sound_trigger',
                  'm5_platform_stackchan_expression_trigger'):
        assert token in result.stdout, (enabled, token)
    if not enabled:
        assert 'return "DISABLED";' in result.stdout

profile = (ROOT / 'm5_beta_app/sdkconfig.stackchan.defaults').read_text()
assert 'CONFIG_M5_PLATFORM_STACKCHAN_VOICE_WS_URI=""' in profile
assert 'CONFIG_M5_PLATFORM_ENROLLMENT_WS_URI=""' in profile
assert 'CONFIG_SR_' not in profile
print('Kizz preprocessing passes: default wake/AFE/microphone absent; sounds, BSP and motion preserved; opt-in source remains available.')
