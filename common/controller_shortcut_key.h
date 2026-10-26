#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Target-neutral recogniser for the keys of a paired BLE media remote, so a
 * button can start a bridge shortcut. The bridge owns what a button does; this
 * only names the key, classifies the gesture and builds the request.
 *
 * Rules that keep a remote usable as a remote:
 *   RESERVED  volume up/down and mute never become shortcuts, so volume always
 *             means volume.
 *   TRANSPORT play/pause, next and previous keep their normal action on the
 *             press. Only a long press can start a shortcut, and the press's
 *             normal action has already run by then.
 *   FREE      every other key reports press, long and double.
 *
 * The recogniser is single-threaded and clockless: callers pass `now_ms` and
 * decide when to call controller_shortcut_gesture_poll().
 */

#define CONTROLLER_SHORTCUT_LONG_PRESS_MS 600u
#define CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS 350u
#define CONTROLLER_SHORTCUT_KEY_NAME_MAX 24u
/* "{"key":"<24>","gesture":"double"}" with room to spare. */
#define CONTROLLER_SHORTCUT_BODY_CAPACITY 80u

typedef enum {
    CONTROLLER_SHORTCUT_KEY_RESERVED = 0,
    CONTROLLER_SHORTCUT_KEY_TRANSPORT,
    CONTROLLER_SHORTCUT_KEY_FREE,
} controller_shortcut_key_class_t;

typedef enum {
    CONTROLLER_SHORTCUT_GESTURE_NONE = 0,
    CONTROLLER_SHORTCUT_GESTURE_PRESS,
    CONTROLLER_SHORTCUT_GESTURE_LONG,
    CONTROLLER_SHORTCUT_GESTURE_DOUBLE,
} controller_shortcut_gesture_t;

typedef struct {
    uint16_t usage;
    controller_shortcut_gesture_t gesture;
} controller_shortcut_event_t;

/* Key name the bridge's map uses: lowercase letters, digits, underscores. A
 * usage this table does not know is "usage_%04x", so any key a remote sends can
 * still be mapped. Returns `buf`, or NULL when it cannot fit. */
const char *controller_shortcut_key_name(uint16_t usage, char *buf, size_t len);
controller_shortcut_key_class_t controller_shortcut_key_class(uint16_t usage);
const char *controller_shortcut_gesture_name(controller_shortcut_gesture_t gesture);

/* `{"key":"...","gesture":"..."}`. False if the event is empty or `len` is
 * too small; nothing is partially written as valid JSON. */
bool controller_shortcut_build_body(const controller_shortcut_event_t *event,
                                    char *out, size_t len);

/* Short, fixed, ASCII text for a bridge outcome `code` (the `code` field of the
 * /knob/input reply). NULL when nothing should be shown: `played` (the music is
 * the feedback) and `unmapped` (most presses are not shortcuts). An unknown code
 * gets a generic failure so a newer bridge never reads as success. The bridge's
 * own sentence is for the web UI; this must fit a small display and its font. */
const char *controller_shortcut_feedback_for_code(const char *code);

typedef struct {
    uint16_t usage;     /* key being tracked, 0 when none */
    bool down;
    bool long_sent;
    bool consumed;      /* this press already produced its event (a double) */
    uint32_t down_at_ms;
    bool single_pending;
    uint16_t single_usage;
    uint32_t single_deadline_ms;
} controller_shortcut_gesture_state_t;

void controller_shortcut_gesture_reset(controller_shortcut_gesture_state_t *state);

/* Feed one key down (`pressed`) or up. Writes up to 2 events (a different key's
 * press first flushes a pending single press) and returns how many. */
size_t controller_shortcut_gesture_on_key(
    controller_shortcut_gesture_state_t *state, uint16_t usage, bool pressed,
    uint32_t now_ms, controller_shortcut_event_t out[2]);

/* Emit whatever has become due: a long press on a held key, or a single press
 * whose double window closed. Returns 0 or 1. */
size_t controller_shortcut_gesture_poll(
    controller_shortcut_gesture_state_t *state, uint32_t now_ms,
    controller_shortcut_event_t *out);

/* Milliseconds until the next poll could emit, or UINT32_MAX if nothing is
 * pending. Lets a one-shot timer replace a polling loop. */
uint32_t controller_shortcut_gesture_next_due_ms(
    const controller_shortcut_gesture_state_t *state, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
