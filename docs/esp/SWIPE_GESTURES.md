# Swipe Gesture Recognition

This document covers how the firmware detects swipe gestures from touch input.

## Overview

Swipe gestures are implemented in software on top of the raw touch coordinates. The touch controller (CST816) reports X/Y positions; the firmware tracks touch start/end points and timing to detect swipes.

## Gesture Parameters

```c
#define SWIPE_MIN_DISTANCE 60    // Minimum pixels traveled
#define SWIPE_MAX_TIME_MS 500    // Maximum gesture duration
```

A valid swipe must:

- Travel at least 60 pixels from start to end
- Complete within 500ms
- Move more in the primary direction than perpendicular

## State Machine

The gesture detector tracks:

```c
static int16_t s_touch_start_x = 0;       // Where touch began
static int16_t s_touch_start_y = 0;
static int64_t s_touch_start_time = 0;    // When touch began (ms)
static bool s_touch_tracking = false;      // Currently tracking a touch?
```

### State Transitions

```
Touch Down (tracking=false)
    │
    ▼
Record start position and time
Set tracking=true
    │
    ▼
Touch Move (tracking=true)
    │ (nothing - we only check on release)
    ▼
Touch Up (tracking=true)
    │
    ├─── elapsed > SWIPE_MAX_TIME_MS? ──► Too slow, not a swipe
    │
    ├─── distance < SWIPE_MIN_DISTANCE? ──► Too short, not a swipe
    │
    └─── Check direction:
         │
         ├─── |dy| > |dx| and dy < 0? ──► Swipe Up
         │
         └─── |dy| > |dx| and dy > 0? ──► Swipe Down
```

## Implementation

Detection happens in the LVGL touch read callback:

```c
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x, y;

    if (tpGetCoordinates(&x, &y)) {
        // Touch active
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;

        // Start tracking new touch
        if (!s_touch_tracking) {
            s_touch_start_x = x;
            s_touch_start_y = y;
            s_touch_start_time = esp_timer_get_time() / 1000;  // us → ms
            s_touch_tracking = true;
        }
    } else {
        // Touch released
        data->state = LV_INDEV_STATE_RELEASED;

        if (s_touch_tracking) {
            int64_t elapsed = (esp_timer_get_time() / 1000) - s_touch_start_time;

            if (elapsed < SWIPE_MAX_TIME_MS) {
                int16_t dx = data->point.x - s_touch_start_x;
                int16_t dy = data->point.y - s_touch_start_y;

                // Swipe up: negative Y, more vertical than horizontal
                if (dy < -SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
                    s_pending_art_mode = true;
                }
                // Swipe down: positive Y, more vertical than horizontal
                else if (dy > SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
                    s_pending_exit_art_mode = true;
                }
            }
            s_touch_tracking = false;
        }
    }
}
```

## Deferred Processing

Gestures set flags rather than acting immediately:

```c
static volatile bool s_pending_art_mode = false;
static volatile bool s_pending_exit_art_mode = false;
```

These flags are processed in the main UI loop:

```c
void platform_display_process_pending(void) {
    if (s_pending_art_mode) {
        s_pending_art_mode = false;
        display_art_mode();
    }
    if (s_pending_exit_art_mode) {
        s_pending_exit_art_mode = false;
        if (display_get_state() == DISPLAY_STATE_ART_MODE) {
            display_wake();
        }
    }
}
```

Why defer? The touch callback runs from LVGL's internal context. Calling display state functions directly could cause threading issues with LVGL's internal state.

## Supported Gestures

| Gesture | Action | Condition |
|---------|--------|-----------|
| Swipe Up | Enter art mode | dy < -60px, time < 500ms |
| Swipe Down | Exit art mode | dy > +60px, time < 500ms |
| Double-tap | Enter art mode | 2 taps within 400ms, < 40px apart |
| Any tap | Exit art mode | (when in art mode) |

Art mode hides the control UI and shows fullscreen album artwork.

### Double-tap Detection

Double-tap provides an alternative to swipe for entering art mode:

```c
#define DOUBLE_TAP_MAX_MS 400      // Max time between taps
#define DOUBLE_TAP_MAX_DISTANCE 40 // Max movement between taps
```

The detector tracks the previous tap location and time. If a second tap occurs within the threshold, it enters art mode. This supplements (not replaces) swipe gestures for users who find swiping difficult.

Note: Exiting art mode only requires a single tap anywhere on the screen, so double-tap detection is skipped when already in art mode.

## Coordinate System

Screen coordinates follow standard convention:

- Origin (0,0) at top-left
- X increases rightward
- Y increases downward

So:

- **Swipe up** = finger moves toward top of screen = negative dy
- **Swipe down** = finger moves toward bottom = positive dy

### Rotation Handling

When the display is rotated 180°, the raw touch coordinates are inverted relative to the user's perspective. The gesture detection compensates by inverting dx/dy when `s_current_rotation == 180`:

```c
// Transform swipe direction for 180° rotation
if (s_current_rotation == 180) {
    dy = -dy;
    dx = -dx;
}
```

This ensures that a user's physical "swipe up" gesture always triggers art mode entry, regardless of display orientation.

## Why Software Implementation?

The CST816 has a hardware gesture detection register, but the firmware uses software detection because:

1. **Reliability** - Hardware gesture detection requires proper configuration and can be finicky
2. **Flexibility** - Software allows tuning thresholds without reflashing touch IC firmware
3. **Debugging** - Easier to log and debug software-based detection

## Adding New Gestures

To add horizontal swipes:

```c
// In the touch release handler:
// Swipe left: negative X, more horizontal than vertical
if (dx < -SWIPE_MIN_DISTANCE && abs(dx) > abs(dy)) {
    // Handle swipe left
}
// Swipe right: positive X, more horizontal than vertical
else if (dx > SWIPE_MIN_DISTANCE && abs(dx) > abs(dy)) {
    // Handle swipe right
}
```

For multi-finger gestures (pinch, rotate), you'd need a touch controller that reports multiple touch points. The CST816 only reports single touch.

## Interaction with LVGL Widgets

Swipe detection runs in parallel with LVGL's input handling. A swipe that starts on a button will:

1. Trigger `LV_EVENT_PRESSED` on the button
2. Trigger `LV_EVENT_PRESS_LOST` as finger moves away
3. Trigger the swipe action on release

This can cause unexpected behavior if a swipe starts on an interactive widget. The current design accepts this tradeoff since swipes are only used for global actions (art mode toggle).

## Timing Considerations

`esp_timer_get_time()` returns microseconds since boot. Dividing by 1000 gives milliseconds:

```c
int64_t now_ms = esp_timer_get_time() / 1000;
```

This timer doesn't overflow for ~292,000 years, so no wraparound handling is needed.
