#include "m5_platform.h"

#include <M5Unified.h>

#include <driver/i2c.h>
#include <esp_log.h>

static const char *TAG = "m5_platform";
static m5_platform_board_t s_board = M5_PLATFORM_BOARD_UNKNOWN;
static bool s_started = false;
static bool s_joystick = false;

namespace {
constexpr i2c_port_t kJoystickI2cPort = I2C_NUM_0;
constexpr uint8_t kJoystickAddress = 0x59;
constexpr gpio_num_t kJoystickSda = GPIO_NUM_38;
constexpr gpio_num_t kJoystickScl = GPIO_NUM_39;

bool joystick_read(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(kJoystickI2cPort, kJoystickAddress,
                                        &reg, 1, data, len,
                                        pdMS_TO_TICKS(20)) == ESP_OK;
}
}

extern "C" bool m5_platform_begin(void) {
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power = true;
    cfg.pmic_button = true;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.led_brightness = 0;

    M5.begin(cfg);
    const auto board = M5.getBoard();
    if (board != m5::board_t::board_M5Tough &&
        board != m5::board_t::board_M5AtomS3) {
        ESP_LOGE(TAG, "Unsupported M5 board detected: %d",
                 static_cast<int>(M5.getBoard()));
        return false;
    }
    if (M5.Display.width() == 0 || M5.Display.height() == 0) {
        ESP_LOGE(TAG, "M5 display is not enabled");
        return false;
    }

    s_joystick = board == m5::board_t::board_M5AtomS3;
    M5.Display.setRotation(s_joystick ? 0 : 1);
    /* Bridge artwork is RGB565 in network byte order. M5GFX's image path
     * needs byte swapping for the Tough panel; primitive colors are
     * unaffected by this setting. */
    M5.Display.setSwapBytes(true);
    M5.Display.setBrightness(180);
    s_board = s_joystick ? M5_PLATFORM_BOARD_ATOMS3_JOYSTICK
                         : M5_PLATFORM_BOARD_TOUGH;
    s_started = true;
    if (s_joystick) {
        i2c_config_t i2c_cfg = {};
        i2c_cfg.mode = I2C_MODE_MASTER;
        i2c_cfg.sda_io_num = kJoystickSda;
        i2c_cfg.scl_io_num = kJoystickScl;
        i2c_cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
        i2c_cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
        i2c_cfg.master.clk_speed = 400000;
        const esp_err_t i2c_config_err = i2c_param_config(kJoystickI2cPort, &i2c_cfg);
        const esp_err_t i2c_install_err = i2c_config_err == ESP_OK
            ? i2c_driver_install(kJoystickI2cPort, I2C_MODE_MASTER, 0, 0, 0)
            : i2c_config_err;
        if (i2c_install_err != ESP_OK && i2c_install_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Atom JoyStick I2C initialization failed");
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
        ESP_LOGI(TAG, "Qualified M5 Tough: %ux%u touch=%s", M5.Display.width(),
                 M5.Display.height(), M5.Touch.isEnabled() ? "yes" : "no");
    }
    return true;
}

extern "C" void m5_platform_update(void) {
    if (s_started) {
        M5.update();
    }
}

extern "C" m5_platform_board_t m5_platform_board(void) {
    return s_board;
}

extern "C" const char *m5_platform_board_name(void) {
    return s_board == M5_PLATFORM_BOARD_TOUGH ? "M5Stack Tough" :
           s_board == M5_PLATFORM_BOARD_ATOMS3_JOYSTICK ? "AtomS3 Joystick" :
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
    } else if (detail.wasHold()) {
        out->state = M5_PLATFORM_TOUCH_HELD;
    } else if (detail.wasClicked()) {
        out->state = M5_PLATFORM_TOUCH_CLICKED;
    } else if (detail.isDragging()) {
        out->state = M5_PLATFORM_TOUCH_DRAGGING;
    } else if (detail.wasReleased()) {
        out->state = M5_PLATFORM_TOUCH_RELEASED;
    } else {
        out->state = M5_PLATFORM_TOUCH_NONE;
    }
    return out->state != M5_PLATFORM_TOUCH_NONE;
}

extern "C" bool m5_platform_joystick_state(m5_platform_joystick_state_t *out) {
    if (!out || !s_started || !s_joystick) return false;
    uint8_t joy[4] = {};
    uint8_t buttons[4] = {};
    if (!joystick_read(0x30, joy, sizeof(joy)) ||
        !joystick_read(0x70, buttons, sizeof(buttons))) return false;
    out->left_x = joy[0];
    out->left_y = joy[1];
    out->right_x = joy[2];
    out->right_y = joy[3];
    out->buttons = (buttons[0] ? 1 : 0) | (buttons[1] ? 2 : 0) |
                   (buttons[2] ? 4 : 0) | (buttons[3] ? 8 : 0);
    return true;
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
