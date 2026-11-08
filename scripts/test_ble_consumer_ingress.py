#!/usr/bin/env python3
"""Exercise the actual HID ingress/owner cases with a deferrable RTOS queue.

Only IDF/FreeRTOS boundaries are replaced. Extracting these bounded sections
keeps the radio/teardown implementation out of a host build while testing the
production timestamp, watermark, overflow and normalization code itself.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'components/rk_ble_hid_host/rk_ble_hid_host.c').read_text()

def section(start, end):
    return source[source.index(start):source.index(end, source.index(start))]

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    for name in ('esp_log',):
        (temp / (name + '.h')).write_text((root / ('tests/fixtures/ble_shortcut/' + name + '.inc')).read_text())
    code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rk_ble_hid_host.h"
#include "common/controller_ble_input.c"
typedef int esp_err_t;
typedef int esp_hid_usage_t;
#define ESP_HID_USAGE_GENERIC 0
#define ESP_HID_USAGE_CCONTROL 1
#define ESP_HID_USAGE_MOUSE 2
#define ESP_LOGD(...) ((void)0)
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdPASS 1
#define portMAX_DELAY 0
#define xSemaphoreTake(lock, timeout) ((void)(lock), (void)(timeout))
#define xSemaphoreGive(lock) ((void)(lock))
static uint32_t clock_ms;
static int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
static bool is_stopping(void) { return false; }
'''
    # Event enum/struct are copied intact, including all union members.
    a = source.index('    EVENT_INPUT,')
    a = source.rfind('typedef enum {', 0, a)
    b = source.index('} service_event_t;', a) + len('} service_event_t;')
    code += source[a:b]
    code += r'''
static struct {
    void *events;
    int status_lock;
    rk_ble_hid_host_config_t config;
    rk_ble_hid_host_status_t status;
    rk_ble_consumer_tracker_t consumer;
} s;
static service_event_t queue[32];
static unsigned queue_count;
static int xQueueSend(void *q, const service_event_t *event, int timeout) {
    (void)q; (void)timeout;
    if (queue_count == 32) return 0;
    queue[queue_count++] = *event; return pdPASS;
}
static bool defer_ui=true;
static unsigned sent_count;
static uint32_t last_press_time;
static controller_shortcut_gesture_t last_gesture;
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    if (!defer_ui) fn(arg);
    return true;
}
bool controller_input_dispatch_action(const controller_action_t *action) {
    ++sent_count; last_gesture=action->value.shortcut.gesture;
    last_press_time=action->value.shortcut.press_time_ms; return true;
}
bool controller_input_post_control(controller_control_intent_t intent) { (void)intent; return true; }
'''
    code += section('static portMUX_TYPE s_input_lock', '\nstatic void status_set_connection')
    code += '\nstatic void consume(const service_event_t *event) { switch(event->type) {\n'
    code += section('    case EVENT_CONSUMER_TICK:', '    case EVENT_DISCOVERY:')
    code += 'default: break; }}\n'
    code += r'''
static void drain_host(void) {
    for (unsigned i=0;i<queue_count;++i) consume(&queue[i]);
    queue_count=0;
}
static void report(uint16_t usage) {
    service_event_t event={.type=EVENT_INPUT};
    event.data.input.usage=ESP_HID_USAGE_CCONTROL;
    event.data.input.len=2;
    event.data.input.data[0]=usage & 255;
    event.data.input.data[1]=usage >> 8;
    enqueue_consumer_event(&event);
}
static void tick(void) {
    service_event_t event={.type=EVENT_CONSUMER_TICK};
    enqueue_consumer_event(&event);
}
static void reset(void) {
    queue_count=0; end_consumer_session(); controller_ble_input_service();
    sent_count=0; clock_ms=10000;
}
int main(void) {
    s.events=&queue; s.status.connected=true;
    s.config.on_consumer_event=controller_ble_input_on_consumer_event;
    for (unsigned ui=0;ui<2;++ui) for (unsigned host=0;host<2;++host)
        for(unsigned hold=599;hold<=601;++hold) {
            reset(); defer_ui=ui;
            report(0x223); if(!host) drain_host();
            clock_ms+=hold; report(0); if(!host) drain_host();
            clock_ms+=350; tick(); clock_ms+=5000;
            drain_host(); controller_ble_input_service();
            assert(sent_count==1);
            assert(last_gesture==(hold<600 ? CONTROLLER_SHORTCUT_GESTURE_PRESS : CONTROLLER_SHORTCUT_GESTURE_LONG));
        }
    reset(); defer_ui=true;
    report(0x223); drain_host();
    for(unsigned i=0;i<40;++i) { clock_ms+=50; tick(); }
    drain_host(); controller_ble_input_service(); assert(sent_count==0);
    /* Repeated held reports after upstream loss cannot invent a new press. */
    report(0x223); clock_ms+=700; tick(); drain_host(); controller_ble_input_service();
    assert(sent_count==0);
    report(0); drain_host(); report(0x223); clock_ms+=601; tick();
    drain_host(); controller_ble_input_service(); assert(sent_count==1);
    reset(); report(0x223); clock_ms+=700; tick();
    cancel_consumer_ingress(); drain_host(); controller_ble_input_service(); assert(sent_count==0);
    reset();
    s.config.on_media_key=controller_ble_input_on_media_key;
    report(0xe9); report(0xea); report(0xcd); report(0xb5); report(0xb6); report(0xe9);
    report(0); report(0x223); queue[queue_count - 1].data.input.usage=0;
    drain_host();
    assert(s.status.recent_report_count==5);
    assert(s.status.recent_reports[0].data[0]==0x23);
    assert(s.status.recent_reports[0].data[1]==0x02);
    assert(s.status.recent_reports[0].media_key==RK_BLE_MEDIA_KEY_NONE);
    assert(s.status.recent_reports[1].data[0]==0);
    assert(s.status.recent_reports[2].media_key==RK_BLE_MEDIA_KEY_VOLUME_UP);
    assert(s.status.recent_reports[4].media_key==RK_BLE_MEDIA_KEY_NEXT_TRACK);
    s.status.connected=false;
    report(0xb5); drain_host();
    assert(s.status.recent_reports[0].data[0]==0x23);
    /* Descriptor-declared selectors are accepted regardless of peer or ID.
     * Identical bytes from unclassified reports do not dispatch a shortcut. */
    const uint8_t consumer_map[] = {
        0x05,0x0c, 0x09,0x01, 0xa1,0x01, 0x85,0x07,
        0x15,0x00, 0x19,0x00, 0x2a,0xff,0x03,
        0x75,0x10, 0x95,0x01, 0x81,0x00,
        0x75,0x08, 0x81,0x01, 0xc0
    };
    assert(rk_ble_hid_report_is_consumer_array(consumer_map, sizeof(consumer_map), 7, 3));
    assert(!rk_ble_hid_report_is_consumer_array(consumer_map, sizeof(consumer_map), 1, 3));
    assert(!rk_ble_hid_report_is_consumer_array(consumer_map, sizeof(consumer_map), 7, 2));
    assert(!rk_ble_hid_report_is_consumer_array(consumer_map, sizeof(consumer_map)-2, 7, 3));
    uint8_t pointer_map[sizeof(consumer_map)];
    memcpy(pointer_map, consumer_map, sizeof(pointer_map));
    pointer_map[1] = 0x01; /* Generic Desktop */
    assert(!rk_ble_hid_report_is_consumer_array(pointer_map, sizeof(pointer_map), 7, 3));
    memcpy(pointer_map, consumer_map, sizeof(pointer_map));
    pointer_map[11] = 1; /* Usage Minimum differs from Logical Minimum */
    assert(!rk_ble_hid_report_is_consumer_array(pointer_map, sizeof(pointer_map), 7, 3));
    s.status.connected=true;
    for (unsigned classified=0; classified<2; ++classified) {
        reset();
        service_event_t home={.type=EVENT_INPUT};
        home.data.input.usage=classified ? ESP_HID_USAGE_CCONTROL : ESP_HID_USAGE_GENERIC;
        home.data.input.report_id=7;
        home.data.input.len=3;
        home.data.input.data[0]=0x23;
        home.data.input.data[1]=0x02;
        enqueue_consumer_event(&home);
        clock_ms+=50;
        memset(home.data.input.data, 0, sizeof(home.data.input.data));
        enqueue_consumer_event(&home);
        clock_ms+=400; tick();
        drain_host(); controller_ble_input_service();
        assert(sent_count==classified);
        assert(s.status.recent_reports[1].consumer_control==(bool)classified);
        if (classified) {
            assert(last_gesture==CONTROLLER_SHORTCUT_GESTURE_PRESS);
            assert(last_press_time==s.status.recent_reports[1].time_ms);
        }
    }
    reset();
    s_learn_started=clock_ms;
    s_learn_until=clock_ms+12000;
    report(0x223); clock_ms+=50; report(0); clock_ms+=400; tick();
    drain_host(); controller_ble_input_service();
    assert(s.status.recent_reports[1].learned);
    assert(sent_count==0);
    reset();
    s_learn_until=0;
    service_event_t down={.type=EVENT_INPUT};
    down.data.input.report_id=7;
    down.data.input.usage=ESP_HID_USAGE_CCONTROL;
    down.data.input.len=3;
    down.data.input.data[0]=0x43;
    enqueue_consumer_event(&down);
    clock_ms+=50;
    down.data.input.data[0]=0;
    enqueue_consumer_event(&down);
    clock_ms+=400; tick();
    drain_host(); controller_ble_input_service();
    assert(sent_count==1);
    assert(s.status.recent_reports[1].consumer_control);
    reset();
    down.data.input.report_id=3;
    down.data.input.usage=ESP_HID_USAGE_MOUSE;
    down.data.input.data[0]=1;
    down.data.input.data[1]=255;
    enqueue_consumer_event(&down);
    clock_ms+=400; tick();
    drain_host(); controller_ble_input_service();
    down.data.input.data[0]=0xcd;
    down.data.input.data[1]=0;
    enqueue_consumer_event(&down);
    clock_ms+=400; tick();
    drain_host(); controller_ble_input_service();
    assert(sent_count==0);
    assert(s.status.recent_reports[0].pointer && !s.status.recent_reports[0].consumer_control);
    puts("HID ingress and both deferred queues preserve gesture history; loss cancels");
}
'''
    (temp / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-fsanitize=address,undefined', '-I'+str(temp), '-I'+str(root),
                    '-I'+str(root/'include'), '-I'+str(root/'common'),
                    '-I'+str(root/'components/rk_ble_hid_host/include'), str(temp/'test.c'),
                    str(root/'common/controller_shortcut_key.c'),
                    str(root/'components/rk_ble_hid_host/rk_ble_hid_report.c'),
                    '-o', str(temp/'test')], check=True)
    subprocess.run([str(temp/'test')], check=True)
