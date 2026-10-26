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
static unsigned posted_controls;
static bool dispatch_accepts = true;

uint64_t platform_millis(void) { return clock_ms; }
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) { fn(arg); return true; }
/* The shortcut leaves the glue as a semantic action through the shared router
 * seam; capture exactly what it dispatches. */
bool controller_input_dispatch_action(const controller_action_t *action) {
    assert(action && action->kind == CONTROLLER_ACTION_SHORTCUT);
    assert(sent_count < 8);
    sent[sent_count].usage = action->value.shortcut.usage;
    sent[sent_count].gesture = (controller_shortcut_gesture_t)action->value.shortcut.gesture;
    ++sent_count;
    return dispatch_accepts;
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

/* The production ingress: raw Consumer Control reports -> the host's tracker ->
 * the controller callback. A release is `00 00` and carries no usage. */
static rk_ble_consumer_tracker_t tracker;
static void report(uint16_t usage) {
    const uint8_t bytes[2] = {(uint8_t)(usage & 0xff), (uint8_t)(usage >> 8)};
    rk_ble_consumer_edge_t edges[2];
    size_t count = rk_ble_consumer_tracker_update(&tracker, bytes, 2, edges);
    for (size_t i = 0; i < count; ++i)
        controller_ble_input_on_consumer_event(edges[i].usage, edges[i].pressed, NULL);
}
static void session_ends(void) {
    rk_ble_consumer_tracker_reset(&tracker);
    controller_ble_input_on_consumer_event(0, false, NULL);
}

static void reset(void) {
    rk_ble_consumer_tracker_reset(&tracker);
    controller_shortcut_gesture_reset(&s_gesture);
    clock_ms = 100000; sent_count = 0; timer_armed = false;
    posted_controls = 0; dispatch_accepts = true;
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

static void a_short_tap_from_real_reports_is_a_press_never_a_long(void) {
    reset();
    report(HOME);
    clock_ms += 60;
    report(0); /* the actual release report: no usage */
    assert(sent_count == 0 && timer_armed); /* waiting out the double window */
    advance_to_timer();
    assert(sent_count == 1 && sent[0].usage == HOME &&
           sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);
    /* ...and the recognizer is free: the next key is tracked and works. */
    clock_ms += 1000;
    report(0x0224);
    advance_to_timer();
    assert(sent_count == 2 && sent[1].usage == 0x0224 &&
           sent[1].gesture == CONTROLLER_SHORTCUT_GESTURE_LONG);
}

static void a_short_play_pause_tap_never_fires_its_long_shortcut(void) {
    reset();
    report(PLAY_PAUSE);
    clock_ms += 80;
    report(0);
    clock_ms += 5000;
    assert(sent_count == 0 && !timer_armed);
}

static void real_double_tap_from_reports(void) {
    reset();
    report(HOME); clock_ms += 50; report(0);
    clock_ms += 120; report(HOME);
    assert(sent_count == 1 && sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_DOUBLE);
    clock_ms += 50; report(0);
    clock_ms += 2000;
    assert(sent_count == 1);
}

static void the_session_ending_drops_a_held_key_and_a_pending_press(void) {
    reset();
    report(HOME);
    assert(timer_armed);
    session_ends(); /* disconnect while the key is held */
    assert(!timer_armed);
    clock_ms += 5000;
    assert(sent_count == 0);

    reset();
    report(HOME); clock_ms += 40; report(0); /* pending single press */
    assert(timer_armed);
    session_ends();
    assert(!timer_armed);
    clock_ms += 5000;
    assert(sent_count == 0);
    /* A key pressed in the next session is tracked from scratch. */
    report(0x0224);
    advance_to_timer();
    assert(sent_count == 1 && sent[0].usage == 0x0224);
}

static void a_refused_dispatch_does_not_wedge_the_recognizer(void) {
    reset();
    dispatch_accepts = false; /* e.g. the router is in a picker context */
    report(HOME);
    advance_to_timer();
    assert(sent_count == 1);
    report(0);
    dispatch_accepts = true;
    clock_ms += 1000;
    report(0x0224);
    advance_to_timer();
    assert(sent_count == 2 && sent[1].usage == 0x0224);
}

int main(void) {
    a_short_tap_from_real_reports_is_a_press_never_a_long();
    a_short_play_pause_tap_never_fires_its_long_shortcut();
    real_double_tap_from_reports();
    the_session_ending_drops_a_held_key_and_a_pending_press();
    long_press_fires_from_the_timer_without_a_release();
    a_tap_waits_for_the_timer_and_a_double_cancels_it();
    volume_and_transport_taps_never_reach_the_bridge();
    a_long_play_pause_is_a_shortcut_and_still_only_that();
    a_refused_dispatch_does_not_wedge_the_recognizer();
    puts("controller BLE shortcut glue tests passed");
    return 0;
}
