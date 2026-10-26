/* The remote-key -> bridge-shortcut glue, driven with a fake clock, timer,
 * UI loop and bridge. Includes the real source so its statics are reachable. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../common/controller_ble_input.c"

static uint64_t clock_ms;
static unsigned timer_creates, timer_starts, timer_stops;
static uint64_t timer_delay_us;
static bool timer_armed;
static void (*timer_cb)(void *);

static controller_shortcut_event_t sent[8];
static unsigned sent_count;
static bool hold_send_tasks;
static void (*held_fn)(void *);
static void *held_arg;
static unsigned posted_controls;

uint64_t platform_millis(void) { return clock_ms; }
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) { fn(arg); return true; }
int platform_task_start_external_stack(const char *name, uint32_t stack,
                                       platform_task_fn_t fn, void *arg) {
    (void)name; (void)stack;
    if (hold_send_tasks) { held_fn = fn; held_arg = arg; return 0; }
    fn(arg);
    return 0;
}
bool bridge_client_send_shortcut(const controller_shortcut_event_t *event) {
    assert(sent_count < 8);
    sent[sent_count++] = *event;
    return true;
}
bool controller_input_post_control(controller_control_intent_t intent) {
    (void)intent; ++posted_controls; return true;
}
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h) {
    timer_cb = a->callback; *h = (esp_timer_handle_t)(void *)&timer_creates; ++timer_creates; return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t h) { (void)h; timer_armed = false; ++timer_stops; return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) {
    (void)h; timer_armed = true; timer_delay_us = us; ++timer_starts; return ESP_OK;
}

enum { HOME = 0x0223, PLAY_PAUSE = 0x00cd, VOL_UP = 0x00e9 };

static void reset(void) {
    controller_shortcut_gesture_reset(&s_gesture);
    clock_ms = 100000; sent_count = 0; timer_armed = false; hold_send_tasks = false;
    held_fn = NULL; held_arg = NULL; posted_controls = 0;
    atomic_store(&s_shortcut_busy, false);
}

static void advance_to_timer(void) {
    assert(timer_armed);
    clock_ms += timer_delay_us / 1000u;
    timer_armed = false;
    timer_cb(NULL);
}

static void long_press_fires_from_the_timer_without_a_release(void) {
    reset();
    controller_ble_input_on_consumer_event(HOME, true, NULL);
    assert(sent_count == 0 && timer_armed);
    assert(timer_delay_us / 1000u >= CONTROLLER_SHORTCUT_LONG_PRESS_MS);
    advance_to_timer();
    assert(sent_count == 1 && sent[0].usage == HOME &&
           sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_LONG);
    assert(!timer_armed);
    controller_ble_input_on_consumer_event(HOME, false, NULL);
    assert(sent_count == 1); /* the release after a long adds nothing */
}

static void a_tap_waits_for_the_timer_and_a_double_cancels_it(void) {
    reset();
    controller_ble_input_on_consumer_event(HOME, true, NULL);
    clock_ms += 60;
    controller_ble_input_on_consumer_event(HOME, false, NULL);
    assert(sent_count == 0 && timer_armed);
    advance_to_timer();
    assert(sent_count == 1 && sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);

    reset();
    controller_ble_input_on_consumer_event(HOME, true, NULL);
    clock_ms += 60;
    controller_ble_input_on_consumer_event(HOME, false, NULL);
    clock_ms += 100;
    controller_ble_input_on_consumer_event(HOME, true, NULL);
    assert(sent_count == 1 && sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_DOUBLE);
    assert(!timer_armed); /* nothing left to wait for */
}

static void volume_and_transport_taps_never_reach_the_bridge(void) {
    reset();
    controller_ble_input_on_consumer_event(VOL_UP, true, NULL);
    controller_ble_input_on_consumer_event(VOL_UP, false, NULL);
    controller_ble_input_on_consumer_event(PLAY_PAUSE, true, NULL);
    clock_ms += 80;
    controller_ble_input_on_consumer_event(PLAY_PAUSE, false, NULL);
    clock_ms += 2000;
    assert(sent_count == 0 && !timer_armed);
    /* Their normal job is the unchanged media-key path. */
    controller_ble_input_on_media_key(RK_BLE_MEDIA_KEY_PLAY_PAUSE, NULL);
    assert(posted_controls == 1);
}

static void a_long_play_pause_is_a_shortcut_and_still_only_that(void) {
    reset();
    controller_ble_input_on_consumer_event(PLAY_PAUSE, true, NULL);
    advance_to_timer();
    assert(sent_count == 1 && sent[0].usage == PLAY_PAUSE &&
           sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_LONG);
}

static void one_shortcut_in_flight_at_a_time_and_none_are_queued(void) {
    reset();
    hold_send_tasks = true;
    controller_ble_input_on_consumer_event(HOME, true, NULL);
    advance_to_timer();
    assert(held_fn && sent_count == 0); /* started, not finished */
    controller_ble_input_on_consumer_event(HOME, false, NULL);
    clock_ms += 1000;
    controller_ble_input_on_consumer_event(0x0224, true, NULL);
    advance_to_timer(); /* a second long while the first is out: dropped */
    assert(held_arg != NULL && sent_count == 0);
    held_fn(held_arg); /* first finishes */
    assert(sent_count == 1 && sent[0].usage == HOME);
    hold_send_tasks = false;
    controller_ble_input_on_consumer_event(0x0224, false, NULL);
    clock_ms += 1000;
    controller_ble_input_on_consumer_event(0x0224, true, NULL);
    advance_to_timer(); /* free again */
    assert(sent_count == 2 && sent[1].usage == 0x0224);
}

int main(void) {
    long_press_fires_from_the_timer_without_a_release();
    a_tap_waits_for_the_timer_and_a_double_cancels_it();
    volume_and_transport_taps_never_reach_the_bridge();
    a_long_play_pause_is_a_shortcut_and_still_only_that();
    one_shortcut_in_flight_at_a_time_and_none_are_queued();
    puts("controller BLE shortcut glue tests passed");
    return 0;
}
