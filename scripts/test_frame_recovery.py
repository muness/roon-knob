#!/usr/bin/env python3
"""Compile the actual drivers/capture backend against fault-injection doubles.
SDK header aliases live in a temporary directory, not production include paths.
"""
import json
import os
import pathlib
import subprocess
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
FRAME_HEADERS = ['driver/gpio.h', 'driver/i2c_master.h', 'driver/spi_master.h',
                 'freertos/FreeRTOS.h', 'freertos/task.h', 'esp_log.h',
                 'esp_heap_caps.h', 'esp_rom_sys.h', 'esp_timer.h']
CRASH_HEADERS = ['esp_idf_version.h', 'sdkconfig.h', 'nvs.h', 'esp_system.h', 'esp_app_desc.h',
                 'esp_core_dump.h', 'esp_partition.h']
with tempfile.TemporaryDirectory(prefix='hiphi-frame-check-') as directory:
    tmp = pathlib.Path(directory)
    for group, headers, sdk in [('frame', FRAME_HEADERS, 'frame_sdk.h'),
                                ('crash', CRASH_HEADERS, 'crash_sdk.h')]:
        for name in headers:
            path = tmp / group / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f'#include "{sdk}"\n')
    def build(name, sources, sdk, defines=()):
        output = tmp / name
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-variable',
                        '-fsanitize=address,undefined', '-pthread',
                        '-I'+str(tmp/sdk), '-Itests', '-Iinclude', '-Icommon',
                        '-Iframe_app/main', *defines, *sources, '-o', str(output)],
                       cwd=ROOT, check=True)
        return output
    for name, sources in [
        ('pmic', ['frame_app/tests/pmic_recovery_test.c']),
        ('display', ['frame_app/tests/display_failure_test.c', 'frame_app/main/frame_display_health.c']),
        ('record', ['tests/test_crash_record.c', 'common/crash_record.c'])]:
        subprocess.run([str(build(name, sources, 'frame'))], check=True)
    capture = build('capture', ['tests/test_crash_capture.c', 'common/crash_report.c',
                               'common/crash_record.c'], 'crash', ['-DESP_PLATFORM'])
    for scenario in ['success', 'commit', 'summary', 'corrupt', 'old-layout', 'normal']:
        subprocess.run([str(capture), scenario], check=True)

    (tmp/'frame'/'esp_http_server.h').write_text('#include "frame_web_sdk.h"\n')
    web = tmp/'web-preview'
    try:
        cjson_flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libcjson'], stderr=subprocess.DEVNULL, text=True).split()
    except subprocess.CalledProcessError:
        # Local IDF builds already resolve the same portable cJSON dependency.
        source = next((ROOT/'frame_app/managed_components').glob('espressif__cjson/**/cJSON.c'))
        cjson_flags = ['-I'+str(source.parent), str(source)]
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-pthread', '-I'+str(tmp/'frame'), '-Itests', '-Iinclude', '-Icommon',
                    '-Iframe_app/main', '-DPORTAL_BRAND_PRODUCT="Frame"',
                    'tests/frame_health_web_preview.c', 'frame_app/main/frame_display_health.c',
                    'common/portal_brand.c', *cjson_flags, '-o', str(web)], cwd=ROOT, check=True)
    page = subprocess.check_output([str(web)])
    report = subprocess.check_output([str(web), 'json'])
    health = json.loads(report)
    assert health['displayed'] == 'art:12345678:1' and health['attempted'] == 'art:87654321:2'
    assert health['error'] == 'panel BUSY timeout' and health['completed'] == 1
    if output := os.environ.get('FRAME_HEALTH_PREVIEW_DIR'):
        destination = pathlib.Path(output)
        destination.mkdir(parents=True, exist_ok=True)
        (destination/'index.html').write_bytes(page)
        (destination/'health.json').write_bytes(report)
    print('Frame health HTTP handler: previous successful image survives later failure')
