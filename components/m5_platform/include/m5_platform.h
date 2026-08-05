#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    M5_PLATFORM_BOARD_UNKNOWN = 0,
    M5_PLATFORM_BOARD_TOUGH = 1,
    M5_PLATFORM_BOARD_ATOMS3_JOYSTICK = 2,
} m5_platform_board_t;

typedef enum {
    M5_PLATFORM_TOUCH_NONE = 0,
    M5_PLATFORM_TOUCH_PRESSED = 1,
    M5_PLATFORM_TOUCH_CLICKED = 2,
    M5_PLATFORM_TOUCH_DRAGGING = 3,
    M5_PLATFORM_TOUCH_RELEASED = 4,
    M5_PLATFORM_TOUCH_HELD = 5,
} m5_platform_touch_state_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t delta_x;
    int16_t delta_y;
    m5_platform_touch_state_t state;
} m5_platform_touch_event_t;

bool m5_platform_begin(void);
void m5_platform_update(void);
m5_platform_board_t m5_platform_board(void);
const char *m5_platform_board_name(void);
uint16_t m5_platform_display_width(void);
uint16_t m5_platform_display_height(void);
bool m5_platform_touch_event(m5_platform_touch_event_t *out);

typedef struct {
    uint16_t left_x;
    uint16_t left_y;
    uint16_t right_x;
    uint16_t right_y;
    /* Atom JoyStick's 0x70 register is active-low and ordered by the
     * coprocessor firmware: top-left, top-right, left-stick press,
     * right-stick press. Keep those physical controls named at the platform
     * boundary so UIs cannot accidentally treat them as one opaque bitmask. */
    bool top_left_pressed;
    bool top_right_pressed;
    bool left_stick_pressed;
    bool right_stick_pressed;
} m5_platform_joystick_state_t;

bool m5_platform_joystick_state(m5_platform_joystick_state_t *out);

typedef struct {
    bool pressed;
    bool clicked;
    bool held;
} m5_platform_surface_button_event_t;

bool m5_platform_surface_button_event(m5_platform_surface_button_event_t *out);

/* Target-neutral display power primitives used by the shared controller
 * timeout policy and M5-family renderers. They must run on the UI task. */
void m5_platform_set_brightness(uint8_t brightness);
void m5_platform_display_sleep(void);
void m5_platform_display_wake(void);

#ifdef __cplusplus
}
#endif
