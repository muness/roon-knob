#include "controller_shortcut_key.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    PLAY_PAUSE = 0x00cd,
    NEXT = 0x00b5,
    VOL_UP = 0x00e9,
    MUTE = 0x00e2,
    HOME = 0x0223,
    UNKNOWN = 0x0192,
};

static controller_shortcut_gesture_state_t g;
static controller_shortcut_event_t ev[2];

static size_t key(uint16_t usage, bool down, uint32_t t) {
    return controller_shortcut_gesture_on_key(&g, usage, down, t, ev);
}

static size_t poll(uint32_t t) {
    return controller_shortcut_gesture_poll(&g, t, &ev[0]);
}

static void names(void) {
    char buf[CONTROLLER_SHORTCUT_KEY_NAME_MAX + 1];
    assert(strcmp(controller_shortcut_key_name(PLAY_PAUSE, buf, sizeof(buf)), "play_pause") == 0);
    assert(strcmp(controller_shortcut_key_name(HOME, buf, sizeof(buf)), "home") == 0);
    /* A key the table does not know can still be mapped on the bridge. */
    assert(strcmp(controller_shortcut_key_name(UNKNOWN, buf, sizeof(buf)), "usage_0192") == 0);
    assert(controller_shortcut_key_name(0, buf, sizeof(buf)) == NULL);
    char tiny[4];
    assert(controller_shortcut_key_name(PLAY_PAUSE, tiny, sizeof(tiny)) == NULL);
    assert(controller_shortcut_key_name(HOME, NULL, 0) == NULL);

    assert(controller_shortcut_key_class(VOL_UP) == CONTROLLER_SHORTCUT_KEY_RESERVED);
    assert(controller_shortcut_key_class(MUTE) == CONTROLLER_SHORTCUT_KEY_RESERVED);
    assert(controller_shortcut_key_class(NEXT) == CONTROLLER_SHORTCUT_KEY_TRANSPORT);
    assert(controller_shortcut_key_class(HOME) == CONTROLLER_SHORTCUT_KEY_FREE);
    assert(controller_shortcut_key_class(UNKNOWN) == CONTROLLER_SHORTCUT_KEY_FREE);
}

static void body(void) {
    char out[CONTROLLER_SHORTCUT_BODY_CAPACITY];
    controller_shortcut_event_t e = {HOME, CONTROLLER_SHORTCUT_GESTURE_LONG};
    assert(controller_shortcut_build_body(&e, out, sizeof(out)));
    assert(strcmp(out, "{\"key\":\"home\",\"gesture\":\"long\"}") == 0);
    e.usage = UNKNOWN;
    e.gesture = CONTROLLER_SHORTCUT_GESTURE_DOUBLE;
    assert(controller_shortcut_build_body(&e, out, sizeof(out)));
    assert(strcmp(out, "{\"key\":\"usage_0192\",\"gesture\":\"double\"}") == 0);
    /* Every name must fit the buffer, whatever the usage. */
    e.usage = 0xffff;
    assert(controller_shortcut_build_body(&e, out, sizeof(out)));

    e.gesture = CONTROLLER_SHORTCUT_GESTURE_NONE;
    assert(!controller_shortcut_build_body(&e, out, sizeof(out)) && out[0] == '\0');
    e.gesture = CONTROLLER_SHORTCUT_GESTURE_PRESS;
    char small[10];
    assert(!controller_shortcut_build_body(&e, small, sizeof(small)) && small[0] == '\0');
    assert(!controller_shortcut_build_body(NULL, out, sizeof(out)));
}

static void free_key_single_press_waits_out_the_double_window(void) {
    controller_shortcut_gesture_reset(&g);
    assert(key(HOME, true, 1000) == 0);
    assert(poll(1100) == 0);
    assert(key(HOME, false, 1100) == 0);
    /* Not reported yet: it might be the first half of a double. */
    assert(poll(1100 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS - 1) == 0);
    assert(poll(1100 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS) == 1);
    assert(ev[0].usage == HOME && ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);
    assert(poll(5000) == 0); /* reported once */
}

static void free_key_double_replaces_both_presses(void) {
    controller_shortcut_gesture_reset(&g);
    assert(key(HOME, true, 1000) == 0);
    assert(key(HOME, false, 1080) == 0);
    assert(key(HOME, true, 1200) == 1);
    assert(ev[0].usage == HOME && ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_DOUBLE);
    assert(key(HOME, false, 1280) == 0);
    /* Neither the release nor time may add a press or a long. */
    assert(poll(1280 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS + 1) == 0);
    assert(poll(1200 + CONTROLLER_SHORTCUT_LONG_PRESS_MS + 1) == 0);
}

static void held_double_does_not_also_become_a_long(void) {
    controller_shortcut_gesture_reset(&g);
    key(HOME, true, 0);
    key(HOME, false, 50);
    assert(key(HOME, true, 100) == 1);
    assert(poll(100 + CONTROLLER_SHORTCUT_LONG_PRESS_MS + 100) == 0);
    assert(controller_shortcut_gesture_next_due_ms(&g, 100) == UINT32_MAX);
}

static void a_second_tap_after_the_window_is_two_presses_not_a_double(void) {
    controller_shortcut_gesture_reset(&g);
    key(HOME, true, 0);
    key(HOME, false, 50);
    assert(poll(50 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS) == 1);
    assert(ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);
    assert(key(HOME, true, 900) == 0);
    key(HOME, false, 950);
    assert(poll(950 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS) == 1);
    assert(ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);
}

static void a_late_second_down_without_a_poll_is_still_not_a_double(void) {
    /* The window closed but nothing polled yet: the second tap arrives late. */
    controller_shortcut_gesture_reset(&g);
    key(HOME, true, 0);
    key(HOME, false, 50);
    size_t n = key(HOME, true, 50 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS + 5);
    assert(n == 1);
    assert(ev[0].usage == HOME && ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);
}

static void a_different_key_flushes_the_pending_press_first(void) {
    controller_shortcut_gesture_reset(&g);
    key(HOME, true, 0);
    key(HOME, false, 50);
    assert(key(UNKNOWN, true, 100) == 1);
    assert(ev[0].usage == HOME && ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);
    key(UNKNOWN, false, 150);
    assert(poll(150 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS) == 1);
    assert(ev[0].usage == UNKNOWN);
}

static void long_press_fires_while_held_and_nothing_else_follows(void) {
    controller_shortcut_gesture_reset(&g);
    key(HOME, true, 1000);
    assert(controller_shortcut_gesture_next_due_ms(&g, 1000) == CONTROLLER_SHORTCUT_LONG_PRESS_MS);
    assert(poll(1000 + CONTROLLER_SHORTCUT_LONG_PRESS_MS - 1) == 0);
    assert(poll(1000 + CONTROLLER_SHORTCUT_LONG_PRESS_MS) == 1);
    assert(ev[0].usage == HOME && ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_LONG);
    assert(poll(9999) == 0);
    key(HOME, false, 2500);
    assert(poll(9999) == 0); /* the release after a long is not a press */
    assert(controller_shortcut_gesture_next_due_ms(&g, 9999) == UINT32_MAX);
}

static void transport_keys_only_ever_report_long(void) {
    controller_shortcut_gesture_reset(&g);
    /* A tap: the key's normal action is the controller's; we report nothing. */
    assert(key(PLAY_PAUSE, true, 0) == 0);
    assert(key(PLAY_PAUSE, false, 80) == 0);
    assert(poll(80 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS + 1) == 0);
    /* Two quick taps are two normal presses, never a double. */
    assert(key(PLAY_PAUSE, true, 200) == 0);
    assert(key(PLAY_PAUSE, false, 260) == 0);
    assert(key(PLAY_PAUSE, true, 300) == 0);
    assert(key(PLAY_PAUSE, false, 340) == 0);
    assert(poll(340 + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS + 1) == 0);
    /* Holding is the only gesture. */
    assert(key(NEXT, true, 1000) == 0);
    assert(poll(1000 + CONTROLLER_SHORTCUT_LONG_PRESS_MS) == 1);
    assert(ev[0].usage == NEXT && ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_LONG);
}

static void reserved_keys_never_produce_anything(void) {
    controller_shortcut_gesture_reset(&g);
    assert(key(VOL_UP, true, 0) == 0);
    assert(poll(CONTROLLER_SHORTCUT_LONG_PRESS_MS * 3) == 0);
    assert(key(VOL_UP, false, 3000) == 0);
    assert(key(MUTE, true, 4000) == 0);
    assert(key(MUTE, false, 4050) == 0);
    assert(poll(9000) == 0);
    assert(controller_shortcut_gesture_next_due_ms(&g, 9000) == UINT32_MAX);
    /* Holding volume must not stop a later real key from being tracked. */
    assert(key(VOL_UP, true, 10000) == 0);
    assert(key(HOME, true, 10010) == 0);
    assert(poll(10010 + CONTROLLER_SHORTCUT_LONG_PRESS_MS) == 1);
    assert(ev[0].usage == HOME);
}

static void stray_releases_and_second_keys_are_ignored(void) {
    controller_shortcut_gesture_reset(&g);
    assert(key(HOME, false, 10) == 0);
    assert(key(0, true, 10) == 0);
    key(HOME, true, 100);
    assert(key(UNKNOWN, true, 120) == 0); /* second key while one is down */
    assert(key(UNKNOWN, false, 130) == 0);
    assert(poll(100 + CONTROLLER_SHORTCUT_LONG_PRESS_MS) == 1);
    assert(ev[0].usage == HOME);
}

static void the_clock_may_wrap(void) {
    controller_shortcut_gesture_reset(&g);
    const uint32_t near_wrap = UINT32_MAX - 100u;
    key(HOME, true, near_wrap);
    assert(poll(near_wrap + 50u) == 0);
    assert(poll(near_wrap + CONTROLLER_SHORTCUT_LONG_PRESS_MS) == 1);
    assert(ev[0].gesture == CONTROLLER_SHORTCUT_GESTURE_LONG);
}

static void feedback(void) {
    assert(controller_shortcut_feedback_for_code("played") == NULL);
    assert(controller_shortcut_feedback_for_code("unmapped") == NULL);
    assert(strcmp(controller_shortcut_feedback_for_code("zone_missing"), "Zone unavailable") == 0);
    assert(strcmp(controller_shortcut_feedback_for_code("no_zone"), "Pick a zone first") == 0);
    assert(strcmp(controller_shortcut_feedback_for_code("item_unavailable"), "Item not found") == 0);
    /* A code from a newer bridge, or none at all, must never read as success. */
    assert(strcmp(controller_shortcut_feedback_for_code("brand_new"), "Shortcut failed") == 0);
    assert(strcmp(controller_shortcut_feedback_for_code(NULL), "Shortcut failed") == 0);
    /* Every message fits a small display and stays ASCII. */
    const char *codes[] = {"no_zone", "zone_missing", "provider_mismatch",
                           "item_unavailable", "no_shortcut", "refused", "x"};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        const char *text = controller_shortcut_feedback_for_code(codes[i]);
        assert(text && strlen(text) <= 24);
        for (const char *c = text; *c; ++c) assert((unsigned char)*c < 0x80);
    }
}

int main(void) {
    feedback();
    names();
    body();
    free_key_single_press_waits_out_the_double_window();
    free_key_double_replaces_both_presses();
    held_double_does_not_also_become_a_long();
    a_second_tap_after_the_window_is_two_presses_not_a_double();
    a_late_second_down_without_a_poll_is_still_not_a_double();
    a_different_key_flushes_the_pending_press_first();
    long_press_fires_while_held_and_nothing_else_follows();
    transport_keys_only_ever_report_long();
    reserved_keys_never_produce_anything();
    stray_releases_and_second_keys_are_ignored();
    the_clock_may_wrap();
    puts("controller shortcut key tests passed");
    return 0;
}
