#include "controller_button_gesture.h"

void controller_button_gesture_reset(controller_button_gesture_t *state) {
    if (!state) {
        return;
    }
    state->single_pending = false;
    state->single_deadline_ms = 0;
}

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

controller_physical_gesture_t controller_button_gesture_on_release(
    controller_button_gesture_t *state, uint32_t now_ms,
    uint32_t double_click_window_ms) {
    if (!state || double_click_window_ms == 0) {
        return CONTROLLER_PHYSICAL_GESTURE_NONE;
    }
    if (state->single_pending &&
        !deadline_reached(now_ms, state->single_deadline_ms)) {
        controller_button_gesture_reset(state);
        return CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP;
    }
    state->single_pending = true;
    state->single_deadline_ms = now_ms + double_click_window_ms;
    return CONTROLLER_PHYSICAL_GESTURE_NONE;
}

controller_physical_gesture_t controller_button_gesture_take_due(
    controller_button_gesture_t *state, uint32_t now_ms) {
    if (!state || !state->single_pending ||
        !deadline_reached(now_ms, state->single_deadline_ms)) {
        return CONTROLLER_PHYSICAL_GESTURE_NONE;
    }
    controller_button_gesture_reset(state);
    return CONTROLLER_PHYSICAL_GESTURE_TAP;
}
