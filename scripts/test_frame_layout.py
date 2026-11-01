#!/usr/bin/env python3
"""Exercise Frame's real preference store and layout-change UI transition."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp/'esp_log.h').write_text('''#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
''')
    (tmp/'nvs.h').write_text('''#include <stdint.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_get_u8(nvs_handle_t,const char*,uint8_t*);
int nvs_set_u8(nvs_handle_t,const char*,uint8_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
''')
    ui = (root/'frame_app/main/eink_ui.c').read_text()
    def extract(signature):
        if signature not in ui: return ''
        start=ui.index(signature);brace=ui.index('{',start)
        depth=1;end=brace+1
        while depth:
            depth += (ui[end]=='{') - (ui[end]=='}');end+=1
        return ui[start:end]
    function=extract('void eink_ui_apply_full_cover(bool full_cover)')
    render=extract('static bool render_artwork(void)')
    code = '''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "frame_display_preferences.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ART_W 800
#define ART_H 450
static uint8_t stored=0, pending;
static bool exists, commit_ok=true;
int nvs_open(const char*n,int m,int*h){*h=1;return 0;}
int nvs_get_u8(int h,const char*k,uint8_t*v){if(strcmp(k,"full_cover")||!exists)return 1;*v=stored;return 0;}
int nvs_set_u8(int h,const char*k,uint8_t v){pending=v;return 0;}
int nvs_commit(int h){if(!commit_ok)return 2;stored=pending;exists=true;return 0;}
void nvs_close(int h){}
struct {bool full_cover,art_dirty,dirty,display_pref_dirty;uint64_t last_change,art_retry_after;char image_key[8];} s_ui;
uint64_t platform_millis(void){return 100;}
void eink_ui_apply_full_cover(bool full_cover);
static const char *requested_scale;
const char *bridge_client_get_artwork_url_for_format_and_scale(char*b,size_t n,int w,int h,int clip,const char*f,const char*s,int crop){
 assert(w==800&&h==450&&clip==0&&crop==0&&!strcmp(f,"eink_acep6"));requested_scale=s;return "url";
}
int platform_http_get_image(const char*u,char**p,size_t*n){return -1;}
void platform_http_free(void*p){}
bool cache_packed_artwork(const uint8_t*p,size_t n){return false;}
'''+function+render+'''
int main(void){
 frame_display_preferences_init();
 assert(!frame_display_preferences_full_cover());
 assert(frame_display_preferences_set_full_cover(true));
 frame_display_preferences_init();assert(frame_display_preferences_full_cover());
 commit_ok=false;assert(!frame_display_preferences_set_full_cover(false));
 assert(frame_display_preferences_full_cover());
 commit_ok=true;assert(frame_display_preferences_set_full_cover(false));
 frame_display_preferences_init();assert(!frame_display_preferences_full_cover());
 exists=true;stored=9;frame_display_preferences_init();assert(!frame_display_preferences_full_cover());
 strcpy(s_ui.image_key,"same");s_ui.art_retry_after=900;
 eink_ui_apply_full_cover(true);
 assert(s_ui.full_cover&&s_ui.art_dirty&&s_ui.dirty&&s_ui.display_pref_dirty);
 assert(!render_artwork()&&!strcmp(requested_scale,"fit"));
 assert(s_ui.last_change==100&&s_ui.art_retry_after==0&&!strcmp(s_ui.image_key,"same"));
 s_ui.art_dirty=s_ui.dirty=s_ui.display_pref_dirty=false;
 eink_ui_apply_full_cover(true);assert(!s_ui.dirty&&!s_ui.art_dirty);
 eink_ui_apply_full_cover(false);assert(!s_ui.full_cover&&s_ui.art_dirty&&s_ui.dirty);
 assert(!render_artwork()&&!strcmp(requested_scale,"cover"));
 puts("Frame layout persists, preserves failed commits and redraws unchanged artwork");
}
'''
    (tmp/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable',
                    '-fsanitize=address,undefined','-I'+str(tmp),'-I'+str(root/'frame_app/main'),
                    str(tmp/'test.c'),str(root/'frame_app/main/frame_display_preferences.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
