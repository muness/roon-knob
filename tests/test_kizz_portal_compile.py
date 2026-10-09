"""Compile the actual shared portal with wake OFF/ON and real project headers.

Only ESP-IDF edges are declarations here. OFF deliberately has no wake include
directory, reproducing the production graph; ON uses the real wake API header.
The firmware matrix remains responsible for SDK compilation and final linkage.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tough_app/main/captive_portal.c'
SDK = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
const char *esp_err_to_name(esp_err_t);
void esp_restart(void);
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_SPIRAM 2
void *heap_caps_calloc(size_t, size_t, unsigned);
void *heap_caps_malloc(size_t, unsigned);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(...) ((void)0)
#define taskEXIT_CRITICAL(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(unsigned);
void vTaskDelete(void *);
typedef void *httpd_handle_t;
typedef struct httpd_req { const char *uri; } httpd_req_t;
typedef struct {
    unsigned server_port, stack_size, max_uri_handlers, task_caps;
    bool (*uri_match_fn)(const char *, const char *, size_t);
    esp_err_t (*open_fn)(void *, int);
    void (*close_fn)(void *, int);
} httpd_config_t;
typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *);
} httpd_uri_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_RESP_USE_STRLEN -1
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
bool httpd_uri_match_wildcard(const char *, const char *, size_t);
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, ssize_t);
esp_err_t httpd_resp_send_chunk(httpd_req_t *, const char *, ssize_t);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
esp_err_t httpd_resp_send_err(httpd_req_t *, int, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
'''
CMAKE = r'''
cmake_minimum_required(VERSION 3.16)
set(HIPHI_M5_TARGET stackchan)
set(COMPONENT_LIB main)
function(idf_build_get_property output property)
    if(property STREQUAL "HIPHI_M5_TARGET")
        set(${output} stackchan PARENT_SCOPE)
    elseif(property STREQUAL "HIPHI_KIZZ_WAKE_WORD")
        set(${output} "${WAKE}" PARENT_SCOPE)
    endif()
endfunction()
function(idf_component_register)
    cmake_parse_arguments(REG "" "" "SRCS;INCLUDE_DIRS;REQUIRES;PRIV_REQUIRES" ${ARGN})
    file(WRITE "${REQUIREMENTS}" "${REG_REQUIRES}")
endfunction()
function(set_property)
endfunction()
function(target_compile_definitions)
    file(WRITE "${DEFINITIONS}" "${ARGN}")
endfunction()
function(target_compile_options)
endfunction()
include("${MAIN_COMPONENT}")
'''

with tempfile.TemporaryDirectory(prefix='hiphi-kizz-portal-') as directory:
    temporary = Path(directory)
    (temporary / 'sdk.h').write_text(SDK)
    (temporary / 'main.cmake').write_text(CMAKE)
    for header in ('esp_err.h', 'esp_heap_caps.h', 'esp_http_server.h',
                   'esp_log.h', 'esp_system.h', 'freertos/FreeRTOS.h',
                   'freertos/task.h'):
        path = temporary / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "sdk.h"\n')
    for enabled in (0, 1):
        subprocess.run(['cmake', f'-DWAKE={"ON" if enabled else "OFF"}',
                        f'-DMAIN_COMPONENT={ROOT / "m5_beta_app/main/CMakeLists.txt"}',
                        f'-DREQUIREMENTS={temporary / "requires.txt"}',
                        f'-DDEFINITIONS={temporary / "defines.txt"}',
                        '-P', str(temporary / 'main.cmake')],
                       capture_output=True, text=True, check=True)
        definitions = (temporary / 'defines.txt').read_text()
        assert f'HIPHI_KIZZ_WAKE_WORD={enabled}' in definitions
        requirements = (temporary / 'requires.txt').read_text().split(';')
        assert ('kizz_wake_word' in requirements) == bool(enabled)
        flags = ['cc', '-std=c11', '-Werror=implicit-function-declaration',
                 '-DHIPHI_M5_TARGET_ID=4', f'-DHIPHI_KIZZ_WAKE_WORD={enabled}',
                 f'-I{temporary}', f'-I{ROOT / "include"}', f'-I{ROOT / "common"}']
        if enabled:
            flags.append(f'-I{ROOT / "components/kizz_wake_word/include"}')
        subprocess.run(flags + ['-c', str(SOURCE), '-o', str(temporary / 'portal.o')],
                       capture_output=True, text=True, check=True)
        result = subprocess.run(flags + ['-E', '-P', str(SOURCE)],
                                capture_output=True, text=True, check=True).stdout
        for token in ('sta_wake_config_get_handler', 'sta_wake_config_post_handler',
                      'kizz_wake_word_get_config', 'kizz_wake_word_configure',
                      '/api/wake-config', 'probability_cutoff'):
            assert (token in result) == bool(enabled), (enabled, token)
        for token in ('/api/settings', '/api/zone', 'Sound volume',
                      'touch_ui_post_stackchan_preferences', 'power_debug_web_register'):
            assert token in result, (enabled, token)
print('Actual Kizz portal compiles OFF without wake headers/routes; ON retains calibration; playback/personality/power routes remain.')
