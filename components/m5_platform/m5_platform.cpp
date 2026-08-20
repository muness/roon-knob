#include "m5_platform.h"
#include "m5_stackchan_choreography.h"
#include "m5_stackchan_voice.h"
#include <M5Unified.h>
#if CONFIG_M5_PLATFORM_EXPECT_DIAL
#include <M5Dial.h>
#elif CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
#include <M5StackChan.h>
#elif CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
#include <AtomJoyStick.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "m5_platform";
static m5_platform_board_t s_board = M5_PLATFORM_BOARD_UNKNOWN;
static bool s_started = false;
static bool s_joystick = false;
static bool s_joystick_ready = false;
static bool s_has_imu = false;
static int32_t s_encoder_remainder = 0;

#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
static AtomJoyStick s_atom_joystick;
#endif

namespace {
enum class StackChanMotionPhase {
    idle,
    first_pose,
    second_pose,
    third_pose,
    fourth_pose,
    returning,
};

struct StackChanMotionState {
    bool enabled = false;
    bool initialized = false;
    bool faulted = false;
    bool has_queued = false;
    m5_platform_stackchan_expression_t pending = M5_PLATFORM_STACKCHAN_CELEBRATE;
    m5_platform_stackchan_expression_t queued = M5_PLATFORM_STACKCHAN_CELEBRATE;
    StackChanMotionPhase phase = StackChanMotionPhase::idle;
    uint8_t dance_variant = 0;
    m5_platform_stackchan_face_cue_t face_cue =
        M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
    int64_t deadline = 0;
} s_stackchan_motion;

struct StackChanVoiceState {
    const m5_stackchan_voice_phrase_t *phrase = nullptr;
    uint8_t note = 0;
    int64_t deadline = 0;
    int64_t last_started[11] = {};
    bool enabled = true;
} s_stackchan_voice;

void stackchan_voice_note(uint8_t index) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    const auto &note = s_stackchan_voice.phrase->notes[index];
    if (!M5.Speaker.tone(note.frequency_hz, note.duration_ms, 0, true)) {
        ESP_LOGE(TAG, "StackChan speaker rejected note %u for %s",
                 static_cast<unsigned>(index), s_stackchan_voice.phrase->name);
        s_stackchan_voice.phrase = nullptr;
        return;
    }
    s_stackchan_voice.deadline = esp_timer_get_time() +
        static_cast<int64_t>(note.duration_ms + note.gap_ms) * 1000;
#else
    (void)index;
#endif
}

void stackchan_motion_fail(const char *reason) {
    ESP_LOGE(TAG, "StackChan body language disabled: %s", reason);
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.Motion.setTorqueEnabled(false);
    M5StackChan.setServoPowerEnabled(false);
#endif
    s_stackchan_motion.faulted = true;
    s_stackchan_motion.enabled = false;
    s_stackchan_motion.phase = StackChanMotionPhase::idle;
    s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
}

void stackchan_pose(int yaw, int pitch, int speed) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.Motion.setTorqueEnabled(true);
    M5StackChan.Motion.move(yaw, pitch, speed);
#else
    (void)yaw;
    (void)pitch;
    (void)speed;
#endif
}

void stackchan_dance_pose(size_t index) {
    const auto &pose =
        M5_STACKCHAN_DANCES[s_stackchan_motion.dance_variant][index];
    s_stackchan_motion.face_cue = pose.face;
    stackchan_pose(pose.yaw_angle, pose.pitch_angle, pose.speed);
}

int64_t stackchan_dance_deadline(size_t index) {
    return esp_timer_get_time() +
           static_cast<int64_t>(
               M5_STACKCHAN_DANCES[s_stackchan_motion.dance_variant][index]
                   .hold_ms) *
               1000;
}

bool expected_board(m5::board_t board) {
#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
    return board == m5::board_t::board_M5AtomS3;
#elif CONFIG_M5_PLATFORM_EXPECT_TOUGH
    return board == m5::board_t::board_M5Tough;
#elif CONFIG_M5_PLATFORM_EXPECT_DIAL
    return board == m5::board_t::board_M5Dial;
#elif CONFIG_M5_PLATFORM_EXPECT_STICKS3
    return board == m5::board_t::board_M5StickS3;
#elif CONFIG_M5_PLATFORM_EXPECT_STOPWATCH
    return board == m5::board_t::board_M5StopWatch;
#elif CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    return board == m5::board_t::board_M5StackChan;
#else
    return true;
#endif
}

uint8_t joystick_adc12_to_u8(uint16_t value) {
    value = std::min<uint16_t>(value, 4095);
    return static_cast<uint8_t>((static_cast<uint32_t>(value) * 255 + 2047) /
                                4095);
}
}

extern "C" bool m5_platform_begin(void) {
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power = true;
    cfg.pmic_button = true;
    cfg.internal_imu = true;
    cfg.internal_rtc = false;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.led_brightness = 0;

#if CONFIG_M5_PLATFORM_EXPECT_DIAL || \
    CONFIG_M5_PLATFORM_EXPECT_STACKCHAN || \
    CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
    initArduino();
#endif
#if CONFIG_M5_PLATFORM_EXPECT_DIAL
    M5Dial.begin(cfg, true, false);
#elif CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.begin();
#else
    M5.begin(cfg);
#endif
    const auto board = M5.getBoard();
    if (board != m5::board_t::board_M5Tough &&
        board != m5::board_t::board_M5AtomS3 &&
        board != m5::board_t::board_M5Dial &&
        board != m5::board_t::board_M5StickS3 &&
        board != m5::board_t::board_M5StopWatch &&
        board != m5::board_t::board_M5StackChan) {
        ESP_LOGE(TAG, "Unsupported M5 board detected: %d",
                 static_cast<int>(M5.getBoard()));
        return false;
    }
    if (!expected_board(board)) {
        ESP_LOGE(TAG, "Firmware/board mismatch: detected %d",
                 static_cast<int>(board));
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString("WRONG FIRMWARE", M5.Display.width() / 2,
                              M5.Display.height() / 2);
        return false;
    }
    if (M5.Display.width() == 0 || M5.Display.height() == 0) {
        ESP_LOGE(TAG, "M5 display is not enabled");
        return false;
    }

    s_joystick = board == m5::board_t::board_M5AtomS3;
    M5.Display.setRotation(
        board == m5::board_t::board_M5Tough ||
        board == m5::board_t::board_M5StackChan ? 1 : 0);
    /* Bridge artwork is RGB565 in network byte order. M5GFX's image path
     * needs byte swapping for the Tough panel; primitive colors are
     * unaffected by this setting. */
    M5.Display.setSwapBytes(true);
    M5.Display.setBrightness(180);
    s_board = board == m5::board_t::board_M5AtomS3 ? M5_PLATFORM_BOARD_ATOMS3_JOYSTICK :
              board == m5::board_t::board_M5Tough ? M5_PLATFORM_BOARD_TOUGH :
              board == m5::board_t::board_M5Dial ? M5_PLATFORM_BOARD_DIAL :
              board == m5::board_t::board_M5StickS3 ? M5_PLATFORM_BOARD_STICKS3 :
              board == m5::board_t::board_M5StopWatch ? M5_PLATFORM_BOARD_STOPWATCH :
              M5_PLATFORM_BOARD_STACKCHAN;
    s_has_imu = M5.Imu.isEnabled();
    s_started = true;
    if (s_joystick) {
#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
        s_joystick_ready = s_atom_joystick.begin();
        if (!s_joystick_ready) {
            ESP_LOGW(TAG, "Official Atom JoyStick library did not find the base");
        } else {
            ESP_LOGI(TAG, "Qualified AtomS3 Joystick via M5Stack library: "
                          "%ux%u firmware=%u",
                     M5.Display.width(), M5.Display.height(),
                     s_atom_joystick.getFirmwareVersion());
        }
#else
        ESP_LOGE(TAG, "AtomS3 Joystick firmware lacks its official board library");
        return false;
#endif
    } else if (s_board == M5_PLATFORM_BOARD_STACKCHAN) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        const auto angles = M5StackChan.Motion.getCurrentAngles();
        if (angles.x < -1280 || angles.x > 1280 ||
            angles.y < 0 || angles.y > 900) {
            stackchan_motion_fail("official BSP returned invalid angles");
        } else {
            M5StackChan.Motion.setAutoAngleSyncEnabled(false);
            M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
            M5StackChan.Motion.setTorqueEnabled(false);
            s_stackchan_motion.initialized = true;
            if (M5.Speaker.isEnabled()) {
                /* M5Unified's own speaker example uses master=64 and channel
                 * 255. The enclosed AW88298 is read from listening distance,
                 * so give the short phrases modest headroom above that while
                 * retaining the official amplifier configuration. */
                M5.Speaker.setVolume(96);
                M5.Speaker.setAllChannelVolume(255);
                if (M5.Speaker.begin()) {
                    ESP_LOGI(TAG,
                             "StackChan proto-voice ready via M5Unified: "
                             "running=%d master=%u channel=%u",
                             M5.Speaker.isRunning(), M5.Speaker.getVolume(),
                             M5.Speaker.getChannelVolume(0));
                } else {
                    ESP_LOGW(TAG, "StackChan M5Unified speaker did not start");
                }
            } else {
                ESP_LOGW(TAG, "StackChan speaker unavailable to M5Unified");
            }
            ESP_LOGI(TAG, "Qualified StackChan via M5Stack BSP: yaw=%d pitch=%d",
                     angles.x, angles.y);
        }
#endif
    } else {
        ESP_LOGI(TAG, "Qualified %s: %ux%u touch=%s imu=%s",
                 m5_platform_board_name(), M5.Display.width(),
                 M5.Display.height(), M5.Touch.isEnabled() ? "yes" : "no",
                 s_has_imu ? "yes" : "no");
    }
    return true;
}

extern "C" void m5_platform_update(void) {
    if (s_started) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        if (s_board == M5_PLATFORM_BOARD_STACKCHAN) {
            M5StackChan.update();
            m5_platform_stackchan_sound_process();
            return;
        }
#elif CONFIG_M5_PLATFORM_EXPECT_DIAL
        if (s_board == M5_PLATFORM_BOARD_DIAL) {
            M5Dial.update();
            return;
        }
#endif
        M5.update();
    }
}

extern "C" m5_platform_board_t m5_platform_board(void) {
    return s_board;
}

extern "C" const char *m5_platform_board_name(void) {
    return s_board == M5_PLATFORM_BOARD_TOUGH ? "M5Stack Tough" :
           s_board == M5_PLATFORM_BOARD_ATOMS3_JOYSTICK ? "AtomS3 Joystick" :
           s_board == M5_PLATFORM_BOARD_DIAL ? "M5 Dial" :
           s_board == M5_PLATFORM_BOARD_STICKS3 ? "M5StickS3" :
           s_board == M5_PLATFORM_BOARD_STOPWATCH ? "M5Stack StopWatch" :
           s_board == M5_PLATFORM_BOARD_STACKCHAN ? "StackChan" :
           "unknown";
}

extern "C" uint16_t m5_platform_display_width(void) {
    return s_started ? static_cast<uint16_t>(M5.Display.width()) : 0;
}

extern "C" uint16_t m5_platform_display_height(void) {
    return s_started ? static_cast<uint16_t>(M5.Display.height()) : 0;
}

extern "C" bool m5_platform_touch_event(m5_platform_touch_event_t *out) {
    if (!out || !s_started || s_joystick) {
        return false;
    }
    const auto &detail = M5.Touch.getDetail(0);
    out->x = detail.x;
    out->y = detail.y;
    out->delta_x = static_cast<int16_t>(detail.deltaX());
    out->delta_y = static_cast<int16_t>(detail.deltaY());
    if (detail.wasPressed()) {
        out->state = M5_PLATFORM_TOUCH_PRESSED;
    } else if (detail.isDragging() || detail.isFlicking() ||
               (detail.isPressed() && (out->delta_x != 0 || out->delta_y != 0))) {
        /* M5Unified's documented drag state is deliberately conservative: it
         * starts after the hold threshold. Preserve raw pressed samples with
         * movement so a normal quick swipe remains scrollable. */
        out->state = M5_PLATFORM_TOUCH_DRAGGING;
    } else if (detail.wasHold()) {
        out->state = M5_PLATFORM_TOUCH_HELD;
    } else if (detail.wasClicked()) {
        out->state = M5_PLATFORM_TOUCH_CLICKED;
    } else if (detail.wasReleased()) {
        out->state = M5_PLATFORM_TOUCH_RELEASED;
    } else {
        out->state = M5_PLATFORM_TOUCH_NONE;
    }
    return out->state != M5_PLATFORM_TOUCH_NONE;
}

extern "C" bool m5_platform_joystick_state(m5_platform_joystick_state_t *out) {
    if (!out || !s_started || !s_joystick || !s_joystick_ready) return false;
#if CONFIG_M5_PLATFORM_EXPECT_ATOMS3_JOYSTICK
    out->left_x = joystick_adc12_to_u8(
        s_atom_joystick.getJoy1ADCValueX(_12bit));
    out->left_y = joystick_adc12_to_u8(
        s_atom_joystick.getJoy1ADCValueY(_12bit));
    out->right_x = joystick_adc12_to_u8(
        s_atom_joystick.getJoy2ADCValueX(_12bit));
    out->right_y = joystick_adc12_to_u8(
        s_atom_joystick.getJoy2ADCValueY(_12bit));
    /* M5Stack's public button values are active-low. */
    out->top_left_pressed = s_atom_joystick.getButtonValue(BUTTON_1) == 0;
    out->top_right_pressed = s_atom_joystick.getButtonValue(BUTTON_2) == 0;
    out->left_stick_pressed = s_atom_joystick.getButtonValue(BUTTON_A) == 0;
    out->right_stick_pressed = s_atom_joystick.getButtonValue(BUTTON_B) == 0;
#else
    return false;
#endif
    return true;
}

extern "C" bool m5_platform_surface_button_event(
    m5_platform_surface_button_event_t *out) {
    if (!out || !s_started) return false;
    out->pressed = M5.BtnA.isPressed();
    out->clicked = M5.BtnA.wasClicked();
    out->single_clicked = M5.BtnA.wasSingleClicked();
    out->double_clicked = M5.BtnA.wasDoubleClicked();
    out->held = M5.BtnA.wasHold();
    out->secondary_pressed = M5.BtnB.isPressed();
    out->secondary_clicked = M5.BtnB.wasClicked();
    out->secondary_held = M5.BtnB.wasHold();
    return out->pressed || out->clicked || out->held ||
           out->single_clicked || out->double_clicked ||
           out->secondary_pressed || out->secondary_clicked ||
           out->secondary_held;
}

extern "C" bool m5_platform_encoder_delta(int32_t *out_delta) {
    if (!out_delta || !s_started || s_board != M5_PLATFORM_BOARD_DIAL) return false;
#if CONFIG_M5_PLATFORM_EXPECT_DIAL
    /* M5Dial's official encoder reports quadrature edges. The controller's
     * human-facing unit is one physical detent, so retain partial edges. */
    s_encoder_remainder += M5Dial.Encoder.readAndReset();
    const int32_t detents = s_encoder_remainder / 4;
    s_encoder_remainder -= detents * 4;
    *out_delta = detents;
    return true;
#else
    return false;
#endif
}

extern "C" bool m5_platform_gyro(float *out_x, float *out_y, float *out_z) {
    if (!out_x || !out_y || !out_z || !s_started || !s_has_imu) return false;
    return M5.Imu.getGyro(out_x, out_y, out_z);
}

extern "C" bool m5_platform_accel(float *out_x, float *out_y, float *out_z) {
    if (!out_x || !out_y || !out_z || !s_started || !s_has_imu) return false;
    return M5.Imu.getAccel(out_x, out_y, out_z);
}

extern "C" bool m5_platform_haptic(uint8_t strength) {
    if (!s_started || s_board != M5_PLATFORM_BOARD_STOPWATCH) return false;
    M5.Power.setVibration(strength);
    return true;
}

extern "C" bool m5_platform_battery_is_charging(void) {
    return s_started &&
           M5.Power.isCharging() == m5::Power_Class::is_charging;
}

extern "C" int m5_platform_battery_level(void) {
    return s_started ? static_cast<int>(M5.Power.getBatteryLevel()) : -1;
}

extern "C" bool m5_platform_stackchan_expression_enable(bool enabled) {
    ESP_LOGI(TAG, "StackChan expression enable=%d started=%d board=%d",
             enabled, s_started, static_cast<int>(s_board));
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN) return false;
    if (!enabled) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        M5StackChan.Motion.setTorqueEnabled(false);
        M5StackChan.setServoPowerEnabled(false);
#endif
        s_stackchan_motion.enabled = false;
        s_stackchan_motion.has_queued = false;
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
        return true;
    }
    if (!s_stackchan_motion.initialized || s_stackchan_motion.faulted) {
        ESP_LOGE(TAG, "StackChan official BSP was not qualified");
        return false;
    }
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    M5StackChan.setServoPowerEnabled(true);
#endif
    s_stackchan_motion.enabled = true;
    return true;
}

extern "C" bool m5_platform_stackchan_expression_trigger(
    m5_platform_stackchan_expression_t expression) {
    if (!s_stackchan_motion.enabled || s_stackchan_motion.faulted) return false;
    if (expression != M5_PLATFORM_STACKCHAN_CELEBRATE &&
        expression != M5_PLATFORM_STACKCHAN_SAD &&
        expression != M5_PLATFORM_STACKCHAN_DANCE) return false;
    if (s_stackchan_motion.phase != StackChanMotionPhase::idle) {
        /* Connection loss is higher-value body language than another track
         * celebration. Queue exactly one sadness event behind an active move. */
        if (expression == M5_PLATFORM_STACKCHAN_SAD) {
            s_stackchan_motion.queued = expression;
            s_stackchan_motion.has_queued = true;
            return true;
        }
        return false;
    }
    s_stackchan_motion.pending = expression;
    if (expression == M5_PLATFORM_STACKCHAN_DANCE) {
        s_stackchan_motion.dance_variant = esp_random() % 8;
    }
    ESP_LOGI(TAG, "StackChan gesture starting: %s",
             expression == M5_PLATFORM_STACKCHAN_DANCE ? "dance" :
             expression == M5_PLATFORM_STACKCHAN_SAD ? "sad" : "celebrate");
    if (expression == M5_PLATFORM_STACKCHAN_DANCE) {
        ESP_LOGI(TAG, "StackChan dance variation=%u",
                 s_stackchan_motion.dance_variant + 1);
    }
    if (expression == M5_PLATFORM_STACKCHAN_DANCE) {
        stackchan_dance_pose(0);
        s_stackchan_motion.deadline = stackchan_dance_deadline(0);
    } else if (expression == M5_PLATFORM_STACKCHAN_SAD) {
        /* A slow look away with the head lowered: readable body language
         * without turning a connection problem into a theatrical routine. */
        stackchan_pose(-90, 0, 240);
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_SAD;
        s_stackchan_motion.deadline = esp_timer_get_time() + 650000;
    } else {
        stackchan_pose(-120, 100, 340);
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_BEAM_LEFT;
        s_stackchan_motion.deadline = esp_timer_get_time() + 560000;
    }
    s_stackchan_motion.phase = StackChanMotionPhase::first_pose;
    return true;
}

extern "C" void m5_platform_stackchan_expression_process(void) {
    if (!s_stackchan_motion.enabled || s_stackchan_motion.faulted ||
        s_stackchan_motion.phase == StackChanMotionPhase::idle ||
        esp_timer_get_time() < s_stackchan_motion.deadline) return;

    if (s_stackchan_motion.phase == StackChanMotionPhase::first_pose) {
        if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE) {
            stackchan_dance_pose(1);
            s_stackchan_motion.deadline = stackchan_dance_deadline(1);
        } else if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_SAD) {
            stackchan_pose(40, 0, 220);
            s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_SAD;
            s_stackchan_motion.deadline = esp_timer_get_time() + 620000;
        } else {
            stackchan_pose(120, 100, 340);
            s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_BEAM_RIGHT;
            s_stackchan_motion.deadline = esp_timer_get_time() + 560000;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::second_pose;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::second_pose) {
        if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE) {
            stackchan_dance_pose(2);
            s_stackchan_motion.phase = StackChanMotionPhase::third_pose;
            s_stackchan_motion.deadline = stackchan_dance_deadline(2);
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::fourth_pose;
        s_stackchan_motion.deadline = esp_timer_get_time() + 250000;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::third_pose) {
        stackchan_dance_pose(3);
        s_stackchan_motion.phase = StackChanMotionPhase::fourth_pose;
        s_stackchan_motion.deadline = stackchan_dance_deadline(3);
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::fourth_pose) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        M5StackChan.Motion.goHome(
            s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE ? 260 : 280);
#endif
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_SETTLE;
        s_stackchan_motion.phase = StackChanMotionPhase::returning;
        s_stackchan_motion.deadline = esp_timer_get_time() + 900000;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::returning) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
        M5StackChan.Motion.setTorqueEnabled(false);
#endif
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
        s_stackchan_motion.face_cue = M5_PLATFORM_STACKCHAN_FACE_NEUTRAL;
        ESP_LOGI(TAG, "StackChan gesture complete via official BSP");
        if (s_stackchan_motion.has_queued) {
            const auto queued = s_stackchan_motion.queued;
            s_stackchan_motion.has_queued = false;
            m5_platform_stackchan_expression_trigger(queued);
        }
    }
}

extern "C" bool m5_platform_stackchan_expression_faulted(void) {
    return s_stackchan_motion.faulted;
}

extern "C" m5_platform_stackchan_face_cue_t
m5_platform_stackchan_face_cue(void) {
    return s_stackchan_motion.face_cue;
}

extern "C" bool m5_platform_stackchan_sound_trigger(
    m5_platform_stackchan_sound_t sound) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN ||
        !s_stackchan_voice.enabled || !M5.Speaker.isEnabled() ||
        sound < M5_PLATFORM_STACKCHAN_SOUND_MORE ||
        sound > M5_PLATFORM_STACKCHAN_SOUND_NEW_ROOM) return false;

    const int64_t now = esp_timer_get_time();
    size_t candidates = 0;
    for (const auto &phrase : M5_STACKCHAN_VOICE_PHRASES)
        if (phrase.sound == sound) ++candidates;
    if (!candidates) return false;

    const size_t wanted = esp_random() % candidates;
    const m5_stackchan_voice_phrase_t *selected = nullptr;
    size_t seen = 0;
    for (const auto &phrase : M5_STACKCHAN_VOICE_PHRASES) {
        if (phrase.sound != sound) continue;
        if (seen++ == wanted) { selected = &phrase; break; }
    }
    if (!selected) return false;

    const auto sound_index = static_cast<size_t>(sound);
    if (s_stackchan_voice.last_started[sound_index] &&
        now - s_stackchan_voice.last_started[sound_index] <
            static_cast<int64_t>(selected->cooldown_ms) * 1000) return false;
    if (s_stackchan_voice.phrase &&
        selected->priority < s_stackchan_voice.phrase->priority) return false;

    s_stackchan_voice.phrase = selected;
    s_stackchan_voice.note = 0;
    s_stackchan_voice.last_started[sound_index] = now;
    ESP_LOGI(TAG, "StackChan voice: %s", selected->name);
    stackchan_voice_note(0);
    return true;
#else
    (void)sound;
    return false;
#endif
}

extern "C" void m5_platform_stackchan_sound_process(void) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_stackchan_voice.phrase ||
        esp_timer_get_time() < s_stackchan_voice.deadline) return;
    ++s_stackchan_voice.note;
    if (s_stackchan_voice.note >= s_stackchan_voice.phrase->note_count) {
        s_stackchan_voice.phrase = nullptr;
        return;
    }
    stackchan_voice_note(s_stackchan_voice.note);
#endif
}

extern "C" bool m5_platform_stackchan_sound_enable(bool enabled) {
#if CONFIG_M5_PLATFORM_EXPECT_STACKCHAN
    if (!s_started || s_board != M5_PLATFORM_BOARD_STACKCHAN ||
        !M5.Speaker.isEnabled()) return false;
    if (enabled && !M5.Speaker.isRunning() && !M5.Speaker.begin()) {
        ESP_LOGE(TAG, "StackChan sounds could not start via M5Unified");
        s_stackchan_voice.enabled = false;
        return false;
    }
    s_stackchan_voice.enabled = enabled;
    if (!enabled) {
        s_stackchan_voice.phrase = nullptr;
        M5.Speaker.stop();
    }
    ESP_LOGI(TAG, "StackChan sounds %s", enabled ? "enabled" : "disabled");
    return true;
#else
    (void)enabled;
    return false;
#endif
}

extern "C" void m5_platform_set_brightness(uint8_t brightness) {
    if (s_started) {
        M5.Display.setBrightness(brightness);
    }
}

extern "C" void m5_platform_display_sleep(void) {
    if (s_started) {
        M5.Display.sleep();
    }
}

extern "C" void m5_platform_display_wake(void) {
    if (s_started) {
        M5.Display.wakeup();
    }
}
