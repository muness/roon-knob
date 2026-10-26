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
#define ESP_HID_USAGE_CCONTROL 1
#define ESP_LOGD(...) ((void)0)
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdPASS 1
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
static controller_shortcut_gesture_t last_gesture;
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    if (!defer_ui) fn(arg);
    return true;
}
bool controller_input_dispatch_action(const controller_action_t *action) {
    ++sent_count; last_gesture=action->value.shortcut.gesture; return true;
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
