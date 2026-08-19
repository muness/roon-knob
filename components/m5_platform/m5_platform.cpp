#include "m5_platform.h"
#include "SCSCL.h"

#include <M5Unified.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "m5_platform";
static m5_platform_board_t s_board = M5_PLATFORM_BOARD_UNKNOWN;
static bool s_started = false;
static bool s_joystick = false;
static bool s_has_imu = false;
static int8_t s_encoder_state = 0;
static int32_t s_encoder_delta = 0;

namespace {
constexpr uint8_t kJoystickAddress = 0x59;
constexpr gpio_num_t kJoystickSda = GPIO_NUM_38;
constexpr gpio_num_t kJoystickScl = GPIO_NUM_39;
constexpr uint8_t kRightStickX12Register = 0x20;
constexpr uint8_t kRightStickY12Register = 0x22;
constexpr uint8_t kStackChanIoAddress = 0x6f;
constexpr uint8_t kStackChanIoVersion = 0x02;
constexpr uint8_t kStackChanIoDirection = 0x03;
constexpr uint8_t kStackChanIoOutput = 0x05;
constexpr uint8_t kStackChanIoPullUp = 0x09;
constexpr uint8_t kStackChanServoPowerBit = 0x01;
constexpr uint32_t kStackChanIoFrequency = 100000;
constexpr uart_port_t kStackChanServoUart = UART_NUM_1;
constexpr int kStackChanServoTx = 6;
constexpr int kStackChanServoRx = 7;
SCSCL s_stackchan_servo_bus;

enum class StackChanMotionPhase {
    idle,
    power_wait,
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
    bool power_ready = false;
    bool has_queued = false;
    m5_platform_stackchan_expression_t pending = M5_PLATFORM_STACKCHAN_CELEBRATE;
    m5_platform_stackchan_expression_t queued = M5_PLATFORM_STACKCHAN_CELEBRATE;
    StackChanMotionPhase phase = StackChanMotionPhase::idle;
    bool neutral_valid = false;
    int yaw_neutral = 0;
    int pitch_neutral = 0;
    int yaw_center = 0;
    int pitch_center = 0;
    uint8_t dance_variant = 0;
    uint8_t return_attempts = 0;
    int64_t deadline = 0;
} s_stackchan_motion;

struct StackChanDancePose {
    int8_t yaw_offset;
    int8_t pitch_offset;
    uint8_t move_time;
    uint16_t settle_ms;
};

/* Four gentle dances, all deliberately bounded to about ten degrees from the
 * resting pose. The factory servo protocol's movement time is used to turn
 * the old sequence of snaps into a relaxed sway. */
constexpr StackChanDancePose kStackChanDances[4][4] = {
    {{-28, 8, 44, 560}, {30, 10, 48, 600}, {16, -12, 42, 520}, {-12, 6, 40, 480}},
    {{-16, -14, 42, 520}, {20, 16, 46, 570}, {-24, 8, 44, 550}, {16, -8, 42, 500}},
    {{-32, 12, 48, 610}, {0, -16, 44, 540}, {30, 10, 48, 600}, {0, 5, 40, 470}},
    {{-22, 8, 44, 550}, {24, -10, 46, 570}, {-14, -14, 44, 540}, {14, 10, 44, 530}},
};

bool stackchan_io_read(uint8_t reg, uint8_t *value) {
    return value && M5.In_I2C.readRegister(
        kStackChanIoAddress, reg, value, 1, kStackChanIoFrequency);
}

bool stackchan_io_write(uint8_t reg, uint8_t value) {
    return M5.In_I2C.writeRegister8(
        kStackChanIoAddress, reg, value, kStackChanIoFrequency);
}

bool stackchan_io_set_bit(uint8_t reg, uint8_t bit, bool enabled) {
    uint8_t value = 0;
    if (!stackchan_io_read(reg, &value)) return false;
    value = enabled ? static_cast<uint8_t>(value | bit)
                    : static_cast<uint8_t>(value & ~bit);
    return stackchan_io_write(reg, value);
}

bool stackchan_power(bool enabled) {
    if (!s_stackchan_motion.power_ready) return false;
    return stackchan_io_set_bit(kStackChanIoOutput,
                                kStackChanServoPowerBit, enabled);
}

int servo_read_position(uint8_t id) {
    return s_stackchan_servo_bus.ReadPos(id);
}

bool servo_write_position(uint8_t id, int position, uint16_t move_time) {
    position = std::clamp(position, 0, 1000);
    return s_stackchan_servo_bus.WritePos(id, position, move_time, 0) == 1;
}

bool stackchan_motion_init() {
    if (s_stackchan_motion.initialized) return true;
    uint8_t version = 0;
    ESP_LOGI(TAG, "Qualifying StackChan servo rail: i2c=%d",
             M5.In_I2C.isEnabled());
    bool expander_ready = false;
    if (M5.In_I2C.isEnabled()) {
        ESP_LOGI(TAG, "StackChan expander scan: 0x6f=%d 0x71=%d",
                 M5.In_I2C.scanID(kStackChanIoAddress, 100000),
                 M5.In_I2C.scanID(0x71, 100000));
        // The PY32L020 on the body board boots asynchronously. The official
        // BSP waits up to 1.2s before declaring the expander absent.
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (stackchan_io_read(kStackChanIoVersion, &version) &&
                version != 0 && version != 0xff) {
                expander_ready = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (!expander_ready ||
        !stackchan_io_set_bit(kStackChanIoDirection,
                              kStackChanServoPowerBit, true) ||
        !stackchan_io_set_bit(kStackChanIoPullUp,
                              kStackChanServoPowerBit, true)) {
        ESP_LOGE(TAG, "StackChan servo power expander qualification failed");
        return false;
    }
    ESP_LOGI(TAG, "StackChan servo power expander version=0x%02x", version);
    s_stackchan_motion.power_ready = true;
    /* Exact M5Stack BSP order: power the servo rail, allow it to boot, then
     * initialize the vendored SCSCL driver on UART1 TX=6/RX=7. The reference
     * firmware leaves the rail up and releases torque while idle. */
    if (!stackchan_power(true)) {
        ESP_LOGE(TAG, "StackChan servo rail did not enable during init");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    if (!s_stackchan_servo_bus.begin(kStackChanServoUart, 1000000,
                                     kStackChanServoTx,
                                     kStackChanServoRx)) {
        ESP_LOGE(TAG, "StackChan reference SCSCL driver init failed");
        stackchan_power(false);
        return false;
    }
    const int yaw = s_stackchan_servo_bus.ReadPos(1);
    const int pitch = s_stackchan_servo_bus.ReadPos(2);
    if (yaw >= 64 && yaw <= 936 && pitch >= 64 && pitch <= 936) {
        s_stackchan_motion.yaw_neutral = yaw;
        s_stackchan_motion.pitch_neutral = pitch;
        s_stackchan_motion.neutral_valid = true;
    }
    s_stackchan_servo_bus.EnableTorque(1, 0);
    s_stackchan_servo_bus.EnableTorque(2, 0);
    s_stackchan_motion.initialized = true;
    ESP_LOGI(TAG,
             "StackChan reference servo driver ready: yaw=%d pitch=%d "
             "errors=%u/%u",
             yaw, pitch, s_stackchan_servo_bus.getLastError(),
             s_stackchan_servo_bus.getState());
    ESP_LOGI(TAG, "StackChan body language armed; torque released while idle");
    return true;
}

void stackchan_motion_fail(const char *reason) {
    ESP_LOGE(TAG, "StackChan body language disabled: %s", reason);
    stackchan_power(false);
    s_stackchan_motion.faulted = true;
    s_stackchan_motion.enabled = false;
    s_stackchan_motion.phase = StackChanMotionPhase::idle;
}

bool stackchan_pose(int yaw, int pitch, uint16_t move_time = 20) {
    return servo_write_position(1, yaw, move_time) &&
           servo_write_position(2, pitch, move_time);
}

bool stackchan_dance_pose(size_t index) {
    const auto &pose = kStackChanDances[s_stackchan_motion.dance_variant][index];
    return stackchan_pose(s_stackchan_motion.yaw_center + pose.yaw_offset,
                          s_stackchan_motion.pitch_center + pose.pitch_offset,
                          pose.move_time);
}

int64_t stackchan_dance_deadline(size_t index) {
    return esp_timer_get_time() +
           static_cast<int64_t>(
               kStackChanDances[s_stackchan_motion.dance_variant][index]
                   .settle_ms) *
               1000;
}

bool stackchan_torque(bool enabled) {
    return s_stackchan_servo_bus.EnableTorque(1, enabled ? 1 : 0) == 1 &&
           s_stackchan_servo_bus.EnableTorque(2, enabled ? 1 : 0) == 1;
}

bool expected_board(m5::board_t board) {
#if CONFIG_M5_PLATFORM_EXPECT_DIAL
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

bool joystick_read(uint8_t reg, uint8_t *data, size_t len) {
    return M5.In_I2C.readRegister(kJoystickAddress, reg, data, len, 400000);
}

uint8_t joystick_adc12_to_u8(const uint8_t *data) {
    uint16_t value = static_cast<uint16_t>(data[0]) |
                     (static_cast<uint16_t>(data[1]) << 8);
    value = std::min<uint16_t>(value, 4095);
    return static_cast<uint8_t>((static_cast<uint32_t>(value) * 255 + 2047) /
                                4095);
}

void update_dial_encoder() {
    if (s_board != M5_PLATFORM_BOARD_DIAL) return;
    const int8_t state = static_cast<int8_t>((gpio_get_level(GPIO_NUM_40) << 1) |
                                             gpio_get_level(GPIO_NUM_41));
    static constexpr int8_t kTransitions[16] = {
        0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
    };
    s_encoder_delta += kTransitions[(s_encoder_state << 2) | state];
    s_encoder_state = state;
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

    M5.begin(cfg);
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
    if (s_board == M5_PLATFORM_BOARD_DIAL) {
        gpio_config_t encoder = {};
        encoder.pin_bit_mask = (1ULL << GPIO_NUM_40) | (1ULL << GPIO_NUM_41);
        encoder.mode = GPIO_MODE_INPUT;
        encoder.pull_up_en = GPIO_PULLUP_ENABLE;
        encoder.pull_down_en = GPIO_PULLDOWN_DISABLE;
        encoder.intr_type = GPIO_INTR_DISABLE;
        if (gpio_config(&encoder) != ESP_OK) {
            ESP_LOGE(TAG, "M5Dial encoder GPIO qualification failed");
            return false;
        }
        s_encoder_state = static_cast<int8_t>((gpio_get_level(GPIO_NUM_40) << 1) |
                                              gpio_get_level(GPIO_NUM_41));
    }
    if (s_joystick) {
        if (!M5.In_I2C.isEnabled() || M5.In_I2C.getSDA() != kJoystickSda ||
            M5.In_I2C.getSCL() != kJoystickScl) {
            ESP_LOGE(TAG, "Atom JoyStick I2C pins are not configured by M5Unified");
            return false;
        }
        uint8_t probe = 0;
        if (!joystick_read(0xFE, &probe, 1)) {
            ESP_LOGW(TAG, "Atom JoyStick coprocessor not responding at 0x59");
        } else {
            ESP_LOGI(TAG, "Qualified AtomS3 Joystick: %ux%u firmware=%u",
                     M5.Display.width(), M5.Display.height(), probe);
        }
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
        M5.update();
        update_dial_encoder();
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
    if (!out || !s_started || !s_joystick) return false;
    uint8_t left_x = 0;
    uint8_t left_y = 0;
    uint8_t right_x = 0;
    uint8_t right_y = 0;
    uint8_t right_x_12[2] = {};
    uint8_t right_y_12[2] = {};
    uint8_t buttons[4] = {};
    /* The official Atom-JoyStick protocol exposes the right stick's 12-bit
     * X/Y values at 0x20 and 0x22. Read those values at the platform boundary
     * and normalize them to the existing 8-bit UI contract. This avoids
     * depending on the coprocessor's separate 8-bit summary registers, which
     * are not updating reliably on the firmware revision in this device. */
    if (!joystick_read(0x10, &left_x, 1) ||
        !joystick_read(0x11, &left_y, 1) ||
        !joystick_read(kRightStickX12Register, right_x_12, sizeof(right_x_12)) ||
        !joystick_read(kRightStickY12Register, right_y_12, sizeof(right_y_12)) ||
        !joystick_read(0x70, buttons, sizeof(buttons))) return false;
    right_x = joystick_adc12_to_u8(right_x_12);
    right_y = joystick_adc12_to_u8(right_y_12);
    out->left_x = left_x;
    out->left_y = left_y;
    out->right_x = right_x;
    out->right_y = right_y;
    /* The Atom JoyStick button register is active-low: 0 means pressed. */
    out->top_left_pressed = buttons[0] == 0;
    out->top_right_pressed = buttons[1] == 0;
    out->left_stick_pressed = buttons[2] == 0;
    out->right_stick_pressed = buttons[3] == 0;
    return true;
}

extern "C" bool m5_platform_surface_button_event(
    m5_platform_surface_button_event_t *out) {
    if (!out || !s_started) return false;
    out->pressed = M5.BtnA.isPressed();
    out->clicked = M5.BtnA.wasClicked();
    out->held = M5.BtnA.wasHold();
    out->secondary_pressed = M5.BtnB.isPressed();
    out->secondary_clicked = M5.BtnB.wasClicked();
    out->secondary_held = M5.BtnB.wasHold();
    return out->pressed || out->clicked || out->held ||
           out->secondary_pressed || out->secondary_clicked ||
           out->secondary_held;
}

extern "C" bool m5_platform_encoder_delta(int32_t *out_delta) {
    if (!out_delta || !s_started || s_board != M5_PLATFORM_BOARD_DIAL) return false;
    /* A complete detent is four quadrature edges. Retain incomplete edges. */
    const int32_t detents = s_encoder_delta / 4;
    s_encoder_delta -= detents * 4;
    *out_delta = detents;
    return true;
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
        stackchan_power(false);
        s_stackchan_motion.enabled = false;
        s_stackchan_motion.has_queued = false;
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
        return true;
    }
    s_stackchan_motion.faulted = false;
    if (!stackchan_motion_init()) {
        stackchan_motion_fail("hardware init failed");
        return false;
    }
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
        s_stackchan_motion.dance_variant = esp_random() % 4;
    }
    ESP_LOGI(TAG, "StackChan gesture starting: %s",
             expression == M5_PLATFORM_STACKCHAN_DANCE ? "dance" :
             expression == M5_PLATFORM_STACKCHAN_SAD ? "sad" : "celebrate");
    if (expression == M5_PLATFORM_STACKCHAN_DANCE) {
        ESP_LOGI(TAG, "StackChan dance variation=%u",
                 s_stackchan_motion.dance_variant + 1);
    }
    if (!stackchan_power(true)) {
        stackchan_motion_fail("servo rail did not enable");
        return false;
    }
    s_stackchan_motion.phase = StackChanMotionPhase::power_wait;
    s_stackchan_motion.deadline = esp_timer_get_time() + 250000;
    return true;
}

extern "C" void m5_platform_stackchan_expression_process(void) {
    if (!s_stackchan_motion.enabled || s_stackchan_motion.faulted ||
        s_stackchan_motion.phase == StackChanMotionPhase::idle ||
        esp_timer_get_time() < s_stackchan_motion.deadline) return;

    if (s_stackchan_motion.phase == StackChanMotionPhase::power_wait) {
        const int yaw = servo_read_position(1);
        const int pitch = servo_read_position(2);
        ESP_LOGI(TAG, "StackChan qualified servo positions: yaw=%d pitch=%d",
                 yaw, pitch);
        /* The official BSP permits raw positions 0..1000. Keep a further
         * 64-step guard band so every gesture can return without clipping. */
        if (yaw < 64 || yaw > 936 || pitch < 64 || pitch > 936) {
            stackchan_motion_fail("servo position qualification failed");
            return;
        }
        /* Reuse the qualified neutral instead of adopting each return's small
         * servo tolerance as the next center and slowly drifting across a
         * listening session. A head moved deliberately while torque is off
         * can still establish a new neutral. */
        if (!s_stackchan_motion.neutral_valid ||
            std::abs(yaw - s_stackchan_motion.yaw_neutral) > 48 ||
            std::abs(pitch - s_stackchan_motion.pitch_neutral) > 48) {
            s_stackchan_motion.yaw_neutral = yaw;
            s_stackchan_motion.pitch_neutral = pitch;
            s_stackchan_motion.neutral_valid = true;
            ESP_LOGI(TAG, "StackChan adopted repositioned neutral: yaw=%d pitch=%d",
                     yaw, pitch);
        }
        s_stackchan_motion.yaw_center = s_stackchan_motion.yaw_neutral;
        s_stackchan_motion.pitch_center = s_stackchan_motion.pitch_neutral;
        s_stackchan_motion.return_attempts = 0;
        const bool dance = s_stackchan_motion.pending ==
                           M5_PLATFORM_STACKCHAN_DANCE;
        const bool celebrate = s_stackchan_motion.pending ==
                               M5_PLATFORM_STACKCHAN_CELEBRATE;
        const int first_yaw = celebrate ? yaw - 40 : yaw;
        const int first_pitch = pitch + (celebrate ? 20 : 40);
        if (!stackchan_torque(true) ||
            !(dance ? stackchan_dance_pose(0)
                    : stackchan_pose(first_yaw, first_pitch))) {
            stackchan_motion_fail("first pose was not acknowledged");
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::first_pose;
        s_stackchan_motion.deadline = dance ? stackchan_dance_deadline(0)
                                            : esp_timer_get_time() + 320000;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::first_pose) {
        if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_CELEBRATE ||
            s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE) {
            const bool dance = s_stackchan_motion.pending ==
                               M5_PLATFORM_STACKCHAN_DANCE;
            if (!(dance ? stackchan_dance_pose(1)
                        : stackchan_pose(s_stackchan_motion.yaw_center + 40,
                                         s_stackchan_motion.pitch_center))) {
                stackchan_motion_fail("second pose was not acknowledged");
                return;
            }
            s_stackchan_motion.phase = StackChanMotionPhase::second_pose;
            s_stackchan_motion.deadline = dance ? stackchan_dance_deadline(1)
                                                : esp_timer_get_time() + 320000;
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::second_pose;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::second_pose) {
        if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE) {
            if (!stackchan_dance_pose(2)) {
                stackchan_motion_fail("third dance pose was not acknowledged");
                return;
            }
            s_stackchan_motion.phase = StackChanMotionPhase::third_pose;
            s_stackchan_motion.deadline = stackchan_dance_deadline(2);
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::fourth_pose;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::third_pose) {
        if (!stackchan_dance_pose(3)) {
            stackchan_motion_fail("fourth dance pose was not acknowledged");
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::fourth_pose;
        s_stackchan_motion.deadline = stackchan_dance_deadline(3);
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::fourth_pose) {
        const uint16_t return_time =
            s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE ? 46 : 20;
        if (!stackchan_pose(s_stackchan_motion.yaw_center,
                            s_stackchan_motion.pitch_center, return_time)) {
            stackchan_motion_fail("neutral return was not acknowledged");
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::returning;
        s_stackchan_motion.deadline = esp_timer_get_time() +
            (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_DANCE
                 ? 600000
                 : 380000);
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::returning) {
        const int yaw = servo_read_position(1);
        const int pitch = servo_read_position(2);
        if (yaw < 0 || pitch < 0) {
            stackchan_motion_fail("neutral return could not be verified");
            return;
        }
        if ((std::abs(yaw - s_stackchan_motion.yaw_center) > 12 ||
             std::abs(pitch - s_stackchan_motion.pitch_center) > 12) &&
            s_stackchan_motion.return_attempts++ < 2) {
            if (!stackchan_pose(s_stackchan_motion.yaw_center,
                                s_stackchan_motion.pitch_center)) {
                stackchan_motion_fail("neutral return retry failed");
                return;
            }
            s_stackchan_motion.deadline = esp_timer_get_time() + 300000;
            return;
        }
        if (std::abs(yaw - s_stackchan_motion.yaw_center) > 12 ||
            std::abs(pitch - s_stackchan_motion.pitch_center) > 12) {
            stackchan_motion_fail("neutral return exceeded its time bound");
            return;
        }
        stackchan_torque(false);
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
        ESP_LOGI(TAG,
                 "StackChan gesture complete; neutral verified, torque released");
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
