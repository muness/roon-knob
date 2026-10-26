/* The remote-key -> bridge-shortcut glue, driven with a fake clock, timer,
 * UI loop and bridge. Includes the real source so its statics are reachable. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../common/controller_ble_input.c"

static uint64_t clock_ms;
static bool delay_ui, reject_wake;
static platform_task_fn_t queued_wake;
static uint32_t session=1, sequence;
#define timer_armed (controller_shortcut_gesture_next_due_ms(&s_gesture, (uint32_t)clock_ms) != UINT32_MAX)
static controller_shortcut_event_t sent[8];
static unsigned sent_count;
static unsigned posted_controls;
static bool dispatch_accepts = true;

uint64_t platform_millis(void) { return clock_ms; }
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    if (reject_wake) return false;
    if (delay_ui) queued_wake=fn; else fn(arg);
    return true;
}
static void drain(void) {
    if (queued_wake) { platform_task_fn_t fn=queued_wake; queued_wake=NULL; fn(NULL); }
    controller_ble_input_service();
}
static void feed_key(uint16_t usage, bool pressed, void *ctx) {
    rk_ble_consumer_event_t event = {.kind=usage ? RK_BLE_CONSUMER_EDGE : RK_BLE_CONSUMER_CANCEL,
        .time_ms=(uint32_t)clock_ms, .session=session, .sequence=++sequence, .usage=usage, .pressed=pressed};
    controller_ble_input_on_consumer_event(&event, ctx);
}
static void tick(void) {
    rk_ble_consumer_event_t event = {.kind=RK_BLE_CONSUMER_TICK,
        .time_ms=(uint32_t)clock_ms, .session=session, .sequence=++sequence};
    controller_ble_input_on_consumer_event(&event, NULL);
}
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
enum { HOME = 0x0223, PLAY_PAUSE = 0x00cd, VOL_UP = 0x00e9 };

/* The production ingress: raw Consumer Control reports -> the host's tracker ->
 * the controller callback. A release is `00 00` and carries no usage. */
static rk_ble_consumer_tracker_t tracker;
static void report(uint16_t usage) {
    const uint8_t bytes[2] = {(uint8_t)(usage & 0xff), (uint8_t)(usage >> 8)};
    rk_ble_consumer_edge_t edges[2];
    size_t count = rk_ble_consumer_tracker_update(&tracker, bytes, 2, edges);
    for (size_t i = 0; i < count; ++i)
        feed_key(edges[i].usage, edges[i].pressed, NULL);
}
static void session_ends(void) {
    rk_ble_consumer_tracker_reset(&tracker);
    ++session; feed_key(0, false, NULL);
}

static void reset(void) {
    rk_ble_consumer_tracker_reset(&tracker);
    controller_shortcut_gesture_reset(&s_gesture);
    delay_ui=false; reject_wake=false; queued_wake=NULL;
    ++session; feed_key(0, false, NULL);
    clock_ms = 100000; sent_count = 0;
    posted_controls = 0; dispatch_accepts = true;
}

static void advance_to_timer(void) {
    uint32_t due=controller_shortcut_gesture_next_due_ms(&s_gesture, (uint32_t)clock_ms);
    assert(due != UINT32_MAX);
    clock_ms += due; tick();
}

static void long_press_fires_from_the_timer_without_a_release(void) {
    reset();
    feed_key(HOME, true, NULL);
    assert(sent_count == 0 && timer_armed);

    advance_to_timer();
    assert(sent_count == 1 && sent[0].usage == HOME &&
           sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_LONG);
    assert(!timer_armed);
    feed_key(HOME, false, NULL);
    assert(sent_count == 1); /* the release after a long adds nothing */
}

static void a_tap_waits_for_the_timer_and_a_double_cancels_it(void) {
    reset();
    feed_key(HOME, true, NULL);
    clock_ms += 60;
    feed_key(HOME, false, NULL);
    assert(sent_count == 0 && timer_armed);
    advance_to_timer();
    assert(sent_count == 1 && sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_PRESS);

    reset();
    feed_key(HOME, true, NULL);
    clock_ms += 60;
    feed_key(HOME, false, NULL);
    clock_ms += 100;
    feed_key(HOME, true, NULL);
    assert(sent_count == 1 && sent[0].gesture == CONTROLLER_SHORTCUT_GESTURE_DOUBLE);
    assert(!timer_armed); /* nothing left to wait for */
}

static void volume_and_transport_taps_never_reach_the_bridge(void) {
    reset();
    feed_key(VOL_UP, true, NULL);
    feed_key(VOL_UP, false, NULL);
    feed_key(PLAY_PAUSE, true, NULL);
    clock_ms += 80;
    feed_key(PLAY_PAUSE, false, NULL);
    clock_ms += 2000;
    assert(sent_count == 0 && !timer_armed);
    /* Their normal job is the unchanged media-key path. */
    controller_ble_input_on_media_key(RK_BLE_MEDIA_KEY_PLAY_PAUSE, NULL);
    assert(posted_controls == 1);
}

static void a_long_play_pause_is_a_shortcut_and_still_only_that(void) {
    reset();
    feed_key(PLAY_PAUSE, true, NULL);
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

static void delayed_sequences_and_boundaries(void) {
    for (unsigned delayed=0; delayed<2; ++delayed) {
        for (unsigned hold=599; hold<=601; ++hold) {
            reset(); delay_ui=delayed;
            clock_ms=UINT32_MAX-300;
            report(HOME); clock_ms+=hold; report(0); clock_ms+=350; tick();
            drain();
            assert(sent_count==1);
            assert(sent[0].gesture==(hold<600 ? CONTROLLER_SHORTCUT_GESTURE_PRESS : CONTROLLER_SHORTCUT_GESTURE_LONG));
        }
        for (unsigned gap=349; gap<=351; ++gap) {
            reset(); delay_ui=delayed;
            report(HOME); clock_ms+=20; report(0);
            clock_ms+=gap; report(HOME); clock_ms+=20; report(0);
            clock_ms+=350; tick(); drain();
            assert(sent_count==(gap<350 ? 1u : 2u));
            assert(sent[0].gesture==(gap<350 ? CONTROLLER_SHORTCUT_GESTURE_DOUBLE : CONTROLLER_SHORTCUT_GESTURE_PRESS));
        }
    }
    reset(); delay_ui=true;
    report(HOME); clock_ms+=60; report(0);
    /* UI wall time is irrelevant until a FIFO ingress watermark arrives. */
    clock_ms+=10000; drain(); assert(sent_count==0);
    tick(); drain(); assert(sent_count==1 && sent[0].gesture==CONTROLLER_SHORTCUT_GESTURE_PRESS);
}
static void loss_cancels_without_fabricating_gestures(void) {
    reset(); delay_ui=true;
    report(HOME);
    for (unsigned i=0; i<40; ++i) { clock_ms+=50; tick(); }
    drain(); assert(sent_count==0);
    report(0); drain(); /* reacquire only after a release */
    report(HOME); clock_ms+=601; tick(); drain(); assert(sent_count==1);
    reset(); delay_ui=true; report(HOME); clock_ms+=700; tick();
    session_ends(); drain(); assert(sent_count==0);
    /* A lost coalesced wake cannot strand a tick or cancellation. */
    reset(); reject_wake=true;
    report(HOME); clock_ms+=601; tick(); controller_ble_input_service();
    assert(sent_count==1);
    session_ends(); controller_ble_input_service();
    clock_ms+=1000; tick(); controller_ble_input_service(); assert(sent_count==1);
    controller_ble_input_stats_t stats; controller_ble_input_stats(&stats);
    assert(stats.overflow && stats.cancelled && stats.accepted);
    reject_wake=false;
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
    delayed_sequences_and_boundaries();
    loss_cancels_without_fabricating_gestures();
    puts("controller BLE shortcut glue tests passed");
    return 0;
}
