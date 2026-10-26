#!/usr/bin/env python3
"""Test target adapter completion, NVS failure and durable/effective divergence."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]

def function(source, signature):
    start=source.index(signature)
    brace=source.index('{',start)
    depth=1; end=brace+1
    while depth:
        depth += (source[end]=='{') - (source[end]=='}')
        end+=1
    return source[start:end]

def without_includes(source):
    return '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))

with tempfile.TemporaryDirectory() as directory:
    temp=Path(directory)
    # Use the real setting type/header, with cJSON opaque because this test never parses.
    (temp/'cJSON.h').write_text('typedef struct cJSON cJSON;\n')
    code=r'''
#include <assert.h>
#include <stdio.h>
#include "controller_device_settings.h"
#define LOGW(...) ((void)0)
static int durable_brightness=50, effective_brightness=10, saves;
static bool save_ok=true;
int platform_touch_load_brightness(void) { return durable_brightness; }
bool platform_touch_save_brightness(int p) { ++saves; if(!save_ok)return false; durable_brightness=p; return true; }
void platform_touch_set_brightness(int p) { effective_brightness=p; }
#define platform_device_settings_apply brightness_apply
'''
    code+=without_includes((root/'common/platform/platform_device_settings_touch_brightness.c').read_text())
    code+=r'''
#undef platform_device_settings_apply
static bool durable_ip=true,effective_ip=false;
bool frame_display_preferences_show_ip(void) { return durable_ip; }
bool frame_display_preferences_set_show_ip(bool value) { ++saves; if(!save_ok)return false; durable_ip=value; return true; }
void eink_ui_apply_show_ip(bool value) { effective_ip=value; }
#define platform_device_settings_apply frame_apply
'''
    code+=without_includes((root/'frame_app/main/platform_device_settings_frame.c').read_text())
    code+=r'''
#undef platform_device_settings_apply
int main(void) {
    controller_device_settings_t value={.present=CDS_BRIGHTNESS,.brightness=50};
    assert(brightness_apply(&value) && effective_brightness==50 && saves==0);
    value.brightness=80; save_ok=false;
    assert(!brightness_apply(&value) && effective_brightness==50 && durable_brightness==50);
    save_ok=true; assert(brightness_apply(&value) && effective_brightness==80);
    int before=saves; assert(brightness_apply(&value) && saves==before);
    value=(controller_device_settings_t){.present=CDS_SHOW_IP,.show_ip=true};
    assert(frame_apply(&value) && effective_ip && saves==before);
    value.show_ip=false; save_ok=false;
    assert(!frame_apply(&value) && effective_ip && durable_ip);
    save_ok=true; assert(frame_apply(&value) && !effective_ip && !durable_ip);
    before=saves; assert(frame_apply(&value) && saves==before);
    puts("Dial/HALO and Frame adapters distinguish durable/effective state and storage failure");
}
'''
    (temp/'adapters.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(temp),'-I'+str(root/'common'),str(temp/'adapters.c'),'-o',str(temp/'adapters')],check=True)
    subprocess.run([str(temp/'adapters')],check=True)
    source=(root/'m5_beta_app/main/touch_ui.cpp').read_text()
    code=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#define HIPHI_M5_TARGET_ID 4
using nvs_handle_t=int;
using esp_err_t=int;
using m5_platform_stackchan_volume_t=int;
constexpr int ESP_OK=0,NVS_READWRITE=1,M5_PLATFORM_STACKCHAN_VOLUME_HIGH=2;
constexpr const char *kStackChanNvsNamespace="rk_kizz";
constexpr const char *kStackChanBodyKey="body",*kStackChanBodyPreferenceKey="body_pref";
constexpr const char *kStackChanSoundKey="sound",*kStackChanSoundPreferenceKey="sound_pref";
constexpr const char *kStackChanVoiceVolumeKey="volume",*kStackChanVoiceVolumePreferenceKey="volume_pref";
static int durable[3],pending[3],writes;
static bool fail_open,fail_set,fail_commit,hardware_ok=true;
static struct { bool body_enabled,sound_enabled; int voice_volume; } s;
int nvs_open(const char *,int,nvs_handle_t *h) { *h=1; memcpy(pending,durable,sizeof(durable)); return fail_open?1:0; }
int nvs_set_u8(nvs_handle_t,const char *key,uint8_t v) {
    if(fail_set)return 1;
    if(!strcmp(key,"body"))pending[0]=v;
    if(!strcmp(key,"sound"))pending[1]=v;
    if(!strcmp(key,"volume"))pending[2]=v;
    return 0;
}
int nvs_commit(nvs_handle_t) { if(fail_commit)return 1; memcpy(durable,pending,sizeof(durable)); ++writes; return 0; }
void nvs_close(nvs_handle_t) {}
bool load_body_enabled(void *) { return durable[0]; }
bool load_sound_enabled(void *) { return durable[1]; }
int load_voice_volume(void *) { return durable[2]; }
void set_body_language(bool v,bool) { if(hardware_ok)s.body_enabled=v; }
void set_sounds(bool v,bool,bool) { if(hardware_ok)s.sound_enabled=v; }
void set_voice_volume(int v,bool,bool) { if(hardware_ok)s.voice_volume=v; }
'''
    for signature in ('bool save_body_enabled(', 'bool save_sound_enabled(', 'bool save_voice_volume(', 'extern "C" bool touch_ui_apply_stackchan_preferences('):
        code+='\n'+function(source,signature)+'\n'
    code+=r'''
int main() {
    for(int fault=0;fault<3;++fault) {
        fail_open=fault==0; fail_set=fault==1; fail_commit=fault==2;
        assert(!touch_ui_apply_stackchan_preferences(true,true,2));
        assert(!durable[0] && !s.body_enabled);
    }
    fail_open=fail_set=fail_commit=false; hardware_ok=false;
    assert(!touch_ui_apply_stackchan_preferences(true,true,2));
    assert(durable[0] && durable[1] && durable[2]==2);
    hardware_ok=true; int before=writes;
    assert(touch_ui_apply_stackchan_preferences(true,true,2));
    assert(s.body_enabled && s.sound_enabled && s.voice_volume==2 && writes==before);
    assert(!touch_ui_apply_stackchan_preferences(true,true,3));
    puts("Kizz actual persistence/application functions reject NVS and hardware failure; retry restores effective state");
}
'''
    (temp/'kizz.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(temp/'kizz.cpp'),'-o',str(temp/'kizz')],check=True)
    subprocess.run([str(temp/'kizz')],check=True)
