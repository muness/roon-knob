#include "controller_shortcut_key.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t usage;
    const char *name;
    controller_shortcut_key_class_t key_class;
} key_entry_t;

/* Consumer Control usages (HID Usage Tables, page 0x0C). Names are what the
 * bridge's controller map stores, so renaming one breaks saved maps. */
static const key_entry_t kKeys[] = {
    {0x00cd, "play_pause", CONTROLLER_SHORTCUT_KEY_TRANSPORT},
    {0x00b0, "play", CONTROLLER_SHORTCUT_KEY_TRANSPORT},
    {0x00b1, "pause", CONTROLLER_SHORTCUT_KEY_TRANSPORT},
    {0x00b5, "next", CONTROLLER_SHORTCUT_KEY_TRANSPORT},
    {0x00b6, "previous", CONTROLLER_SHORTCUT_KEY_TRANSPORT},
    {0x00e9, "volume_up", CONTROLLER_SHORTCUT_KEY_RESERVED},
    {0x00ea, "volume_down", CONTROLLER_SHORTCUT_KEY_RESERVED},
    {0x00e2, "mute", CONTROLLER_SHORTCUT_KEY_RESERVED},
    {0x00b7, "stop", CONTROLLER_SHORTCUT_KEY_FREE},
    {0x00b3, "fast_forward", CONTROLLER_SHORTCUT_KEY_FREE},
    {0x00b4, "rewind", CONTROLLER_SHORTCUT_KEY_FREE},
    {0x0040, "menu", CONTROLLER_SHORTCUT_KEY_FREE},
    {0x0223, "home", CONTROLLER_SHORTCUT_KEY_FREE},
    {0x0224, "back", CONTROLLER_SHORTCUT_KEY_FREE},
};

static const key_entry_t *find_key(uint16_t usage) {
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
        if (kKeys[i].usage == usage) return &kKeys[i];
    }
    return NULL;
}

const char *controller_shortcut_key_name(uint16_t usage, char *buf, size_t len) {
    if (!buf || len == 0 || usage == 0) return NULL;
    const key_entry_t *known = find_key(usage);
    int written = known ? snprintf(buf, len, "%s", known->name)
                        : snprintf(buf, len, "usage_%04x", (unsigned)usage);
    if (written < 0 || (size_t)written >= len) {
        buf[0] = '\0';
        return NULL;
    }
    return buf;
}

controller_shortcut_key_class_t controller_shortcut_key_class(uint16_t usage) {
    const key_entry_t *known = find_key(usage);
    return known ? known->key_class : CONTROLLER_SHORTCUT_KEY_FREE;
}

const char *controller_shortcut_gesture_name(controller_shortcut_gesture_t gesture) {
    switch (gesture) {
    case CONTROLLER_SHORTCUT_GESTURE_PRESS: return "press";
    case CONTROLLER_SHORTCUT_GESTURE_LONG: return "long";
    case CONTROLLER_SHORTCUT_GESTURE_DOUBLE: return "double";
    default: return NULL;
    }
}

bool controller_shortcut_build_body(const controller_shortcut_event_t *event,
                                    char *out, size_t len) {
    if (!event || !out || len == 0) return false;
    out[0] = '\0';
    const char *gesture = controller_shortcut_gesture_name(event->gesture);
    char key[CONTROLLER_SHORTCUT_KEY_NAME_MAX + 1];
    if (!gesture || !controller_shortcut_key_name(event->usage, key, sizeof(key))) {
        return false;
    }
    int written = snprintf(out, len, "{\"key\":\"%s\",\"gesture\":\"%s\"}", key, gesture);
    if (written < 0 || (size_t)written >= len) {
        out[0] = '\0';
        return false;
    }
    return true;
}

const char *controller_shortcut_feedback_for_code(const char *code) {
    if (!code) return "Shortcut failed";
    if (!strcmp(code, "accepted") || !strcmp(code, "unmapped")) return NULL;
    if (!strcmp(code, "no_zone")) return "Pick a zone first";
    if (!strcmp(code, "zone_missing")) return "Zone unavailable";
    if (!strcmp(code, "provider_mismatch")) return "Not for this zone";
    if (!strcmp(code, "item_unavailable")) return "Item not found";
    if (!strcmp(code, "no_shortcut")) return "Shortcut was deleted";
    return "Shortcut failed";
}

void controller_shortcut_gesture_reset(controller_shortcut_gesture_state_t *state) {
    if (state) memset(state, 0, sizeof(*state));
}

static bool reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void emit(controller_shortcut_event_t *out, size_t *count, uint16_t usage,
                 controller_shortcut_gesture_t gesture) {
    out[*count].usage = usage;
    out[*count].gesture = gesture;
    ++*count;
}

size_t controller_shortcut_gesture_on_key(
    controller_shortcut_gesture_state_t *state, uint16_t usage, bool pressed,
    uint32_t now_ms, controller_shortcut_event_t out[2]) {
    size_t count = 0;
    if (!state || !out || usage == 0) return 0;
    const controller_shortcut_key_class_t key_class =
        controller_shortcut_key_class(usage);
    if (key_class == CONTROLLER_SHORTCUT_KEY_RESERVED) return 0;

    if (pressed) {
        if (state->down) return 0; /* one key at a time; a second is ignored */
        if (state->single_pending && state->single_usage == usage &&
            !reached(now_ms, state->single_deadline_ms)) {
            state->single_pending = false;
            state->usage = usage;
            state->down = true;
            state->long_sent = false;
            state->consumed = true; /* its release and hold must not repeat it */
            state->down_at_ms = now_ms;
            emit(out, &count, usage, CONTROLLER_SHORTCUT_GESTURE_DOUBLE);
            return count;
        }
        if (state->single_pending) {
            emit(out, &count, state->single_usage, CONTROLLER_SHORTCUT_GESTURE_PRESS);
            state->single_pending = false;
        }
        state->usage = usage;
        state->down = true;
        state->long_sent = false;
        state->consumed = false;
        state->down_at_ms = now_ms;
        return count;
    }

    if (!state->down || state->usage != usage) return 0;
    state->down = false;
    if (key_class == CONTROLLER_SHORTCUT_KEY_FREE && !state->long_sent &&
        !state->consumed) {
        state->single_pending = true;
        state->single_usage = usage;
        state->single_deadline_ms = now_ms + CONTROLLER_SHORTCUT_DOUBLE_WINDOW_MS;
    }
    state->usage = 0;
    return 0;
}

size_t controller_shortcut_gesture_poll(
    controller_shortcut_gesture_state_t *state, uint32_t now_ms,
    controller_shortcut_event_t *out) {
    if (!state || !out) return 0;
    if (state->down && !state->long_sent && !state->consumed &&
        reached(now_ms, state->down_at_ms + CONTROLLER_SHORTCUT_LONG_PRESS_MS)) {
        state->long_sent = true;
        out->usage = state->usage;
        out->gesture = CONTROLLER_SHORTCUT_GESTURE_LONG;
        return 1;
    }
    if (state->single_pending && reached(now_ms, state->single_deadline_ms)) {
        state->single_pending = false;
        out->usage = state->single_usage;
        out->gesture = CONTROLLER_SHORTCUT_GESTURE_PRESS;
        return 1;
    }
    return 0;
}

uint32_t controller_shortcut_gesture_next_due_ms(
    const controller_shortcut_gesture_state_t *state, uint32_t now_ms) {
    uint32_t due = UINT32_MAX;
    if (!state) return due;
    if (state->down && !state->long_sent && !state->consumed) {
        uint32_t at = state->down_at_ms + CONTROLLER_SHORTCUT_LONG_PRESS_MS;
        uint32_t wait = reached(now_ms, at) ? 0u : at - now_ms;
        if (wait < due) due = wait;
    }
    if (state->single_pending) {
        uint32_t wait = reached(now_ms, state->single_deadline_ms)
                            ? 0u
                            : state->single_deadline_ms - now_ms;
        if (wait < due) due = wait;
    }
    return due;
}
