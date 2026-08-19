#include "m5_platform.h"

#include <M5Unified.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
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
constexpr uart_port_t kStackChanServoUart = UART_NUM_1;
constexpr int kStackChanServoTx = 6;
constexpr int kStackChanServoRx = 7;
constexpr uint8_t kServoGoalPosition = 42;
constexpr uint8_t kServoPresentPosition = 56;

enum class StackChanMotionPhase {
    idle,
    power_wait,
    first_pose,
    second_pose,
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
    int yaw_center = 0;
    int pitch_center = 0;
    uint8_t return_attempts = 0;
    int64_t deadline = 0;
} s_stackchan_motion;

bool stackchan_io_read(uint8_t reg, uint8_t *value) {
    return value && M5.In_I2C.readRegister(
        kStackChanIoAddress, reg, value, 1, 400000);
}

bool stackchan_io_write(uint8_t reg, uint8_t value) {
    return M5.In_I2C.writeRegister8(
        kStackChanIoAddress, reg, value, 400000);
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

constexpr uint8_t servo_checksum(const uint8_t *bytes, size_t begin, size_t end) {
    uint8_t sum = 0;
    for (size_t i = begin; i < end; ++i) sum += bytes[i];
    return static_cast<uint8_t>(~sum);
}

constexpr uint8_t kReadPositionPacketWithoutChecksum[] = {
    0xff, 0xff, 1, 4, 0x02, kServoPresentPosition, 2,
};
static_assert(servo_checksum(kReadPositionPacketWithoutChecksum, 2, 7) == 0xbe,
              "SCSCL position-read packet checksum drifted");

bool servo_read_reply(uint8_t id, uint8_t *data, size_t data_len) {
    uint8_t reply[16] = {};
    const size_t need = data_len + 6;
    if (need > sizeof(reply)) return false;
    const int got = uart_read_bytes(kStackChanServoUart, reply, need,
                                    pdMS_TO_TICKS(35));
    if (got != static_cast<int>(need) || reply[0] != 0xff ||
        reply[1] != 0xff || reply[2] != id ||
        reply[3] != data_len + 2 || reply[4] != 0 ||
        reply[need - 1] != servo_checksum(reply, 2, need - 1)) return false;
    if (data_len) std::memcpy(data, reply + 5, data_len);
    return true;
}

bool servo_request(uint8_t id, uint8_t instruction, uint8_t reg,
                   const uint8_t *data, size_t data_len, size_t reply_len) {
    uint8_t packet[16] = {0xff, 0xff, id,
                          static_cast<uint8_t>(data_len + 3), instruction, reg};
    if (data_len) std::memcpy(packet + 6, data, data_len);
    const size_t length = data_len + 7;
    packet[length - 1] = servo_checksum(packet, 2, length - 1);
    uart_flush_input(kStackChanServoUart);
    if (uart_write_bytes(kStackChanServoUart, packet, length) !=
            static_cast<int>(length) ||
        uart_wait_tx_done(kStackChanServoUart, pdMS_TO_TICKS(20)) != ESP_OK)
        return false;
    uint8_t reply[8] = {};
    return servo_read_reply(id, reply, reply_len);
}

int servo_read_position(uint8_t id) {
    const uint8_t count = 2;
    uint8_t packet[8] = {0xff, 0xff, id, 4, 0x02,
                         kServoPresentPosition, count, 0};
    packet[7] = servo_checksum(packet, 2, 7);
    uart_flush_input(kStackChanServoUart);
    if (uart_write_bytes(kStackChanServoUart, packet, sizeof(packet)) !=
            sizeof(packet) ||
        uart_wait_tx_done(kStackChanServoUart, pdMS_TO_TICKS(20)) != ESP_OK)
        return -1;
    uint8_t data[2] = {};
    if (!servo_read_reply(id, data, sizeof(data))) return -1;
    return (static_cast<int>(data[0]) << 8) | data[1];
}

bool servo_write_position(uint8_t id, int position) {
    position = std::clamp(position, 0, 1000);
    const uint8_t data[6] = {
        static_cast<uint8_t>(position >> 8), static_cast<uint8_t>(position),
        0, 20, 0, 0,
    };
    return servo_request(id, 0x03, kServoGoalPosition, data, sizeof(data), 0);
}

bool stackchan_motion_init() {
    if (s_stackchan_motion.initialized) return true;
    uint8_t version = 0;
    if (!M5.In_I2C.isEnabled() ||
        !stackchan_io_read(kStackChanIoVersion, &version) ||
        version == 0 || version == 0xff ||
        !stackchan_io_set_bit(kStackChanIoDirection,
                              kStackChanServoPowerBit, true) ||
        !stackchan_io_set_bit(kStackChanIoPullUp,
                              kStackChanServoPowerBit, true)) {
        ESP_LOGE(TAG, "StackChan servo power expander qualification failed");
        return false;
    }
    s_stackchan_motion.power_ready = true;
    stackchan_power(false);

    const uart_config_t uart_cfg = {
        .baud_rate = 1000000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    if (uart_param_config(kStackChanServoUart, &uart_cfg) != ESP_OK ||
        uart_set_pin(kStackChanServoUart, kStackChanServoTx,
                     kStackChanServoRx, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK ||
        (!uart_is_driver_installed(kStackChanServoUart) &&
         uart_driver_install(kStackChanServoUart, 256, 0, 0, nullptr, 0) != ESP_OK)) {
        ESP_LOGE(TAG, "StackChan servo UART qualification failed");
        return false;
    }
    s_stackchan_motion.initialized = true;
    ESP_LOGI(TAG, "StackChan body language armed; servo rail remains off");
    return true;
}

void stackchan_motion_fail(const char *reason) {
    ESP_LOGE(TAG, "StackChan body language disabled: %s", reason);
    stackchan_power(false);
    s_stackchan_motion.faulted = true;
    s_stackchan_motion.enabled = false;
    s_stackchan_motion.phase = StackChanMotionPhase::idle;
}

bool stackchan_pose(int yaw, int pitch) {
    return servo_write_position(1, yaw) && servo_write_position(2, pitch);
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
        expression != M5_PLATFORM_STACKCHAN_SAD) return false;
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
        /* The official BSP permits raw positions 0..1000. Keep a further
         * 64-step guard band so every gesture can return without clipping. */
        if (yaw < 64 || yaw > 936 || pitch < 64 || pitch > 936) {
            stackchan_motion_fail("servo position qualification failed");
            return;
        }
        s_stackchan_motion.yaw_center = yaw;
        s_stackchan_motion.pitch_center = pitch;
        s_stackchan_motion.return_attempts = 0;
        const int first_yaw = s_stackchan_motion.pending ==
                M5_PLATFORM_STACKCHAN_CELEBRATE ? yaw - 40 : yaw;
        const int first_pitch = pitch +
            (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_CELEBRATE
                 ? 20 : 40);
        if (!stackchan_pose(first_yaw, first_pitch)) {
            stackchan_motion_fail("first pose was not acknowledged");
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::first_pose;
        s_stackchan_motion.deadline = esp_timer_get_time() + 320000;
        return;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::first_pose) {
        if (s_stackchan_motion.pending == M5_PLATFORM_STACKCHAN_CELEBRATE) {
            if (!stackchan_pose(s_stackchan_motion.yaw_center + 40,
                                s_stackchan_motion.pitch_center)) {
                stackchan_motion_fail("second pose was not acknowledged");
                return;
            }
            s_stackchan_motion.phase = StackChanMotionPhase::second_pose;
            s_stackchan_motion.deadline = esp_timer_get_time() + 320000;
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::second_pose;
    }

    if (s_stackchan_motion.phase == StackChanMotionPhase::second_pose) {
        if (!stackchan_pose(s_stackchan_motion.yaw_center,
                            s_stackchan_motion.pitch_center)) {
            stackchan_motion_fail("neutral return was not acknowledged");
            return;
        }
        s_stackchan_motion.phase = StackChanMotionPhase::returning;
        s_stackchan_motion.deadline = esp_timer_get_time() + 380000;
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
        stackchan_power(false);
        s_stackchan_motion.phase = StackChanMotionPhase::idle;
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
