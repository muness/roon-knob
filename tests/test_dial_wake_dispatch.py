"""Execute the firmware's wake/encoder path and UI loop against host fakes.

Only the selected functions are compiled: this tests their integration ordering
and timing, not panel hardware, FreeRTOS synchronization, or the complete build.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"(?:void|bool) " + name + r"\([^)]*\)\s*\{", source)
    if not match:
        raise AssertionError(f"missing function {name}")
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


sleep = (ROOT / "idf_app/main/display_sleep.c").read_text()
input_source = (ROOT / "idf_app/main/platform_input_idf.c").read_text()
ui = (ROOT / "common/ui.c").read_text()
state_enum = re.search(r"typedef enum \{[^}]+\} display_state_t;",
                       (ROOT / "idf_app/main/display_sleep.h").read_text()).group()
constants = "\n".join(re.search(r"^#define " + name + r" .+$", sleep, re.M).group()
                       for name in ("ENCODER_SUPPRESS_AFTER_WAKE_MS", "TOUCH_SUPPRESS_AFTER_WAKE_MS"))
prologue = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "controller_input.h"
#include "controller_input_mailbox.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define LOCK_DISPLAY_STATE() ((void)0)
#define UNLOCK_DISPLAY_STATE() ((void)0)
#define BACKLIGHT_NORMAL 100
#define LVGL_TASK_PRIORITY_NORMAL 2
#define pdTRUE 1
static int64_t now_ms, s_touch_suppress_until_ms, s_encoder_suppress_until_ms;
static display_state_t s_display_state;
static uint32_t s_art_mode_timeout_ms, s_dim_timeout_ms, s_sleep_timeout_ms;
static unsigned s_runtime_wake_count, s_last_logged_overflow;
static void *s_panel_handle, *s_lvgl_task_handle;
static void *s_art_mode_timer, *s_dim_timer, *s_sleep_timer, *s_deep_sleep_timer;
static bool s_debug_sleep_override_armed;
static void *s_input_queue = (void *)1;
static int queued_ticks, dispatches;
static uint32_t coalesced;
static int timer_calls, pending_calls, poll_calls;
static int64_t esp_timer_get_time(void) { return now_ms * 1000; }
static display_state_t display_get_state(void) { return s_display_state; }
static bool panel_exit_sleep_mode(void) { return true; }
static void esp_restart(void) { assert(false); }
static void display_set_backlight(uint8_t v) { (void)v; }
static void ui_set_controls_visible(bool v) { assert(v); }
static void vTaskPrioritySet(void *h, int p) { (void)h; (void)p; }
static void esp_timer_stop(void *h) { (void)h; }
static void esp_timer_start_once(void *h, uint64_t us) { (void)h; (void)us; }
static int xQueueReceive(void *q, int *v, int wait) {
    (void)q; (void)wait;
    if (!queued_ticks) return 0;
    *v = queued_ticks;
    queued_ticks = 0;
    return pdTRUE;
}
static void record_coalesced(uint32_t n) { coalesced += n; }
controller_input_mailbox_stats_t platform_input_mailbox_stats(void) {
    return (controller_input_mailbox_stats_t){0};
}
bool controller_input_dispatch_physical(const controller_physical_event_t *e) {
    assert(e->kind == CONTROLLER_PHYSICAL_EVENT_ROTATION);
    assert(e->value == 3);
    dispatches++;
    return true;
}
static void lv_timer_handler(void) { timer_calls++; }
#define lv_task_handler lv_timer_handler
static void platform_task_run_pending(void) { pending_calls++; }
static void poll_pending(void *p) { assert(!p); poll_calls++; }
'''
main = r'''
int main(void) {
    for (display_state_t state = DISPLAY_STATE_ART_MODE; state <= DISPLAY_STATE_SLEEP; state++) {
        now_ms = 1000;
        s_display_state = state;
        s_panel_handle = (void *)1;
        s_encoder_suppress_until_ms = 0;
        dispatches = 0;
        queued_ticks = 3;
        platform_input_process_events();
        assert(s_display_state == DISPLAY_STATE_NORMAL);
        assert(dispatches == 0 && queued_ticks == 0);
        now_ms = 1499;
        queued_ticks = 3;
        platform_input_process_events();
        assert(dispatches == 0);
        now_ms = 1500;
        queued_ticks = 3;
        platform_input_process_events();
        assert(dispatches == 1); /* Normal activity must not extend suppression. */
        now_ms = 1501;
        queued_ticks = 3;
        platform_input_process_events();
        assert(dispatches == 2);
    }
    /* The input consumer also retains an already-established deep-wake deadline. */
    s_display_state = DISPLAY_STATE_NORMAL;
    now_ms = 2000;
    s_encoder_suppress_until_ms = 2500;
    dispatches = 0;
    queued_ticks = 3;
    platform_input_process_events();
    assert(dispatches == 0);
    now_ms = 2500;
    queued_ticks = 3;
    platform_input_process_events();
    assert(dispatches == 1);
    ui_loop_iter();
    assert(timer_calls == 1 && pending_calls == 1 && poll_calls == 1);
    puts("Dial wake consumes initiating batch, resumes at 500ms; UI timer runs once");
}
'''
functions = "\n".join(function(sleep, name) for name in
                      ("display_wake", "display_activity_detected", "display_is_encoder_suppressed"))
functions += "\n" + function(input_source, "platform_input_process_events")
functions += "\n" + function(ui, "ui_loop_iter")
with tempfile.TemporaryDirectory(prefix="dial-wake-dispatch-") as directory:
    source = Path(directory) / "test.c"
    binary = Path(directory) / "test"
    # The enum/constants precede the host fake declarations that use them.
    source.write_text("#include <stdbool.h>\n#include <stdint.h>\n" + state_enum + "\n" +
                      constants + "\n" + prologue + "\n" + functions + "\n" + main)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(ROOT / "common"),
                    str(source), str(ROOT / "common/controller_input_mailbox.c"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
