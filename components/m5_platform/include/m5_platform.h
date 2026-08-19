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
    M5_PLATFORM_BOARD_DIAL = 3,
    M5_PLATFORM_BOARD_STICKS3 = 4,
    M5_PLATFORM_BOARD_STOPWATCH = 5,
    M5_PLATFORM_BOARD_STACKCHAN = 6,
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
    /* Keep the official library's physical controls named at the platform
     * boundary so UIs cannot accidentally treat them as an opaque bitmask. */
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
    bool secondary_pressed;
    bool secondary_clicked;
    bool secondary_held;
} m5_platform_surface_button_event_t;

bool m5_platform_surface_button_event(m5_platform_surface_button_event_t *out);

/* Form-native capabilities. Unsupported capabilities return false and leave
 * the caller's output untouched. Encoder delta is signed and consumed. */
bool m5_platform_encoder_delta(int32_t *out_delta);
bool m5_platform_gyro(float *out_x, float *out_y, float *out_z);
bool m5_platform_accel(float *out_x, float *out_y, float *out_z);
bool m5_platform_haptic(uint8_t strength);
bool m5_platform_battery_is_charging(void);
int m5_platform_battery_level(void);

typedef enum {
    M5_PLATFORM_STACKCHAN_CELEBRATE = 1,
    M5_PLATFORM_STACKCHAN_SAD = 2,
    M5_PLATFORM_STACKCHAN_DANCE = 3,
} m5_platform_stackchan_expression_t;

/* Face cues are the visual half of one coordinated performance. They expose
 * no servo state or timing detail; the UI maps them to expressive features. */
typedef enum {
    M5_PLATFORM_STACKCHAN_FACE_NEUTRAL = 0,
    M5_PLATFORM_STACKCHAN_FACE_ANTICIPATE = 1,
    M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT = 2,
    M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT = 3,
    M5_PLATFORM_STACKCHAN_FACE_POP = 4,
    M5_PLATFORM_STACKCHAN_FACE_WINK = 5,
    M5_PLATFORM_STACKCHAN_FACE_SAD = 6,
    M5_PLATFORM_STACKCHAN_FACE_SETTLE = 7,
} m5_platform_stackchan_face_cue_t;

/* StackChan body language is enabled by default (and can be turned off with a
 * long hold). Choreography is ours; calibration, limits, power, motion, and
 * torque are owned by M5Stack's official StackChan BSP. */
bool m5_platform_stackchan_expression_enable(bool enabled);
bool m5_platform_stackchan_expression_trigger(
    m5_platform_stackchan_expression_t expression);
void m5_platform_stackchan_expression_process(void);
bool m5_platform_stackchan_expression_faulted(void);
m5_platform_stackchan_face_cue_t m5_platform_stackchan_face_cue(void);

/* Target-neutral display power primitives used by the shared controller
 * timeout policy and M5-family renderers. They must run on the UI task. */
void m5_platform_set_brightness(uint8_t brightness);
void m5_platform_display_sleep(void);
void m5_platform_display_wake(void);

#ifdef __cplusplus
}
#endif
