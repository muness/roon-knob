#pragma once

#include "controller_input.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Small target-neutral recognizer for a button whose single-click action must
 * be delayed until the double-click window closes.  Hardware adapters retain
 * responsibility for debounce and timestamps; this code only owns gesture
 * classification so button-only targets do not each invent subtly different
 * click semantics.
 */
typedef struct {
    bool single_pending;
    uint32_t single_deadline_ms;
} controller_button_gesture_t;

void controller_button_gesture_reset(controller_button_gesture_t *state);

/* Returns DOUBLE_TAP when a pending single is completed, otherwise NONE. */
controller_physical_gesture_t controller_button_gesture_on_release(
    controller_button_gesture_t *state, uint32_t now_ms,
    uint32_t double_click_window_ms);

/* Returns TAP when a pending single has matured, otherwise NONE. */
controller_physical_gesture_t controller_button_gesture_take_due(
    controller_button_gesture_t *state, uint32_t now_ms);
