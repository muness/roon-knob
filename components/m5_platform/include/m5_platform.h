#pragma once

#include <stdbool.h>
#include <stddef.h>
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
typedef bool (*m5_platform_voice_zone_provider_t)(char *out, size_t len);
void m5_platform_voice_set_zone_provider(
    m5_platform_voice_zone_provider_t provider);
void m5_platform_voice_network_ready(void);
void m5_platform_voice_feedback(const char *state);
bool m5_platform_voice_is_listening(void);
const char *m5_platform_voice_state(void);
void m5_platform_voice_copy_transcript(char *out, size_t len);
void m5_platform_voice_copy_response(char *out, size_t len);
void m5_platform_voice_clear_conversation(void);
float m5_platform_voice_wake_probability(void);
float m5_platform_voice_wake_cutoff(void);
bool m5_platform_voice_diagnostics_enabled(void);
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
    bool single_clicked;
    bool double_clicked;
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
    M5_PLATFORM_POWER_SOURCE_UNKNOWN = 0,
    M5_PLATFORM_POWER_SOURCE_BATTERY,
    M5_PLATFORM_POWER_SOURCE_EXTERNAL,
} m5_platform_power_source_t;

typedef struct {
    int battery_level;
    m5_platform_power_source_t source;
    /* Policy is explicit because some portable boards can measure battery
     * but cannot observe USB/VBUS. */
    bool external_power_policy;
} m5_platform_power_snapshot_t;

/* One cached, coherent PMIC/ADC read for a whole controller poll cycle. */
bool m5_platform_power_snapshot(m5_platform_power_snapshot_t *out);

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
    M5_PLATFORM_STACKCHAN_FACE_ATTENTIVE = 8,
    M5_PLATFORM_STACKCHAN_FACE_RESTING = 9,
    M5_PLATFORM_STACKCHAN_FACE_CURIOUS = 10,
    M5_PLATFORM_STACKCHAN_FACE_RELIEVED = 11,
    M5_PLATFORM_STACKCHAN_FACE_GLANCE_LEFT = 12,
    M5_PLATFORM_STACKCHAN_FACE_GLANCE_RIGHT = 13,
    M5_PLATFORM_STACKCHAN_FACE_LOUD = 14,
    M5_PLATFORM_STACKCHAN_FACE_HUSH = 15,
    M5_PLATFORM_STACKCHAN_FACE_PROUD = 16,
    M5_PLATFORM_STACKCHAN_FACE_SHY = 17,
    M5_PLATFORM_STACKCHAN_FACE_WORRIED = 18,
    /* Kismet's basis-expression families. The earlier cues above are the
     * music-product vocabulary; these make the underlying affect library
     * available without inventing a servo gesture for every emotion. */
    M5_PLATFORM_STACKCHAN_FACE_CONTENT = 19,
    M5_PLATFORM_STACKCHAN_FACE_ACCEPTING = 20,
    M5_PLATFORM_STACKCHAN_FACE_STERN = 21,
    M5_PLATFORM_STACKCHAN_FACE_ANGER = 22,
    M5_PLATFORM_STACKCHAN_FACE_DISGUST = 23,
    M5_PLATFORM_STACKCHAN_FACE_FEAR = 24,
    M5_PLATFORM_STACKCHAN_FACE_BORED = 25,
} m5_platform_stackchan_face_cue_t;

/* Kizz body language is enabled by default (and can be turned off with a
 * long hold). Choreography is ours; calibration, limits, power, motion, and
 * torque are owned by M5Stack's official M5StackChan BSP. */
bool m5_platform_stackchan_expression_enable(bool enabled);
bool m5_platform_stackchan_expression_trigger(
    m5_platform_stackchan_expression_t expression);
void m5_platform_stackchan_expression_process(void);
bool m5_platform_stackchan_expression_faulted(void);
m5_platform_stackchan_face_cue_t m5_platform_stackchan_face_cue(void);

typedef enum {
    M5_PLATFORM_STACKCHAN_SOUND_MORE = 1,
    M5_PLATFORM_STACKCHAN_SOUND_LESS = 2,
    M5_PLATFORM_STACKCHAN_SOUND_PREVIOUS = 3,
    M5_PLATFORM_STACKCHAN_SOUND_NEXT = 4,
    M5_PLATFORM_STACKCHAN_SOUND_PLAY = 5,
    M5_PLATFORM_STACKCHAN_SOUND_PAUSE = 6,
    M5_PLATFORM_STACKCHAN_SOUND_CONNECTED = 7,
    M5_PLATFORM_STACKCHAN_SOUND_LOST = 8,
    M5_PLATFORM_STACKCHAN_SOUND_NEW_TRACK = 9,
    M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM = 10,
} m5_platform_stackchan_sound_t;

typedef enum {
    /* LOW deliberately preserves the hardware-tested 96 gain. */
    M5_PLATFORM_STACKCHAN_VOLUME_LOW = 0,
    M5_PLATFORM_STACKCHAN_VOLUME_MEDIUM = 1,
    M5_PLATFORM_STACKCHAN_VOLUME_HIGH = 2,
} m5_platform_stackchan_volume_t;

/* Short affective pitch contours played through M5Unified's Speaker API.
 * They are original UI sounds, not sampled Kismet audio. */
bool m5_platform_stackchan_sound_trigger(m5_platform_stackchan_sound_t sound);
void m5_platform_stackchan_sound_process(void);
bool m5_platform_stackchan_sound_enable(bool enabled);
bool m5_platform_stackchan_sound_volume(
    m5_platform_stackchan_volume_t volume);

/* Target-neutral display power primitives used by the shared controller
 * timeout policy and M5-family renderers. They must run on the UI task. */
void m5_platform_set_brightness(uint8_t brightness);
void m5_platform_display_sleep(void);
void m5_platform_display_wake(void);

/* Enter the exact board's M5Unified-qualified PMIC/power-hold shutdown path.
 * This is a reboot boundary and normally does not return; the physical power
 * button is the recovery path for the supported M5 beta boards. */
void m5_platform_power_off(void);

#ifdef __cplusplus
}
#endif
