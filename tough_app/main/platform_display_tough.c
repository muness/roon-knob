// platform_display.h + platform_display_tough.h implementation for M5Stack
// Tough: AXP192 power sequencing, ILI9342C SPI panel, CHSC6540 I2C touch.
//
// See docs/esp/hw-reference/board-tough.md for the sourced hardware facts
// this file implements (AXP192 register sequence, CHSC6540 register
// protocol, pin assignments) -- both were read directly from M5Stack's own
// open-source m5stack/M5GFX driver during this session, not guessed from the
// "M5 product family" name (see AGENTS.md hardware-claims constraint).
//
// TODO(hardware-verify): nothing in this file has been exercised against the
// owner's physical Tough unit yet (see #193). Treat every timing constant and
// the fixed backlight level as a starting point, not a validated value.

#include "platform_display_tough.h"
#include "platform/platform_display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "display";

// SPI panel pins (docs/esp/hw-reference/board-tough.md)
#define TOUGH_LCD_HOST SPI2_HOST
#define TOUGH_LCD_H_RES 320
#define TOUGH_LCD_V_RES 240
#define TOUGH_PIN_LCD_MOSI GPIO_NUM_23
#define TOUGH_PIN_LCD_MISO GPIO_NUM_38
#define TOUGH_PIN_LCD_SCLK GPIO_NUM_18
#define TOUGH_PIN_LCD_CS   GPIO_NUM_5
#define TOUGH_PIN_LCD_DC   GPIO_NUM_15
// Draw buffers live in internal DMA-capable RAM (SPI DMA cannot target
// PSRAM). 40 rows is a starting point, not a measured watermark.
#define LVGL_BUF_HEIGHT 40

// Shared I2C bus (AXP192 PMIC + CHSC6540 touch)
#define TOUGH_I2C_PORT I2C_NUM_0
#define TOUGH_PIN_I2C_SDA GPIO_NUM_21
#define TOUGH_PIN_I2C_SCL GPIO_NUM_22
#define TOUGH_I2C_FREQ_HZ 400000
#define AXP192_I2C_ADDR 0x34
#define CHSC6540_I2C_ADDR 0x2E

static lv_display_t *s_display = NULL;
static lv_indev_t *s_touch_indev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_axp192_dev = NULL;
static i2c_master_dev_handle_t s_touch_dev = NULL;
static bool s_hardware_ready = false;
static bool s_lvgl_ready = false;

// ── AXP192 (PMIC) minimal power sequencing ─────────────────────────────────
// Register semantics and sequence transcribed from m5stack/M5GFX's verified
// Core2/Tough boot path (see docs/esp/hw-reference/board-tough.md).

static esp_err_t axp192_write_masked(uint8_t reg, uint8_t data, uint8_t mask) {
    uint8_t old_value = 0;
    esp_err_t err = i2c_master_transmit_receive(s_axp192_dev, &reg, 1,
                                                &old_value, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP192 read reg 0x%02x failed: %s", reg,
                 esp_err_to_name(err));
        return err;
    }
    uint8_t buf[2] = {reg, (uint8_t)((old_value & mask) | data)};
    err = i2c_master_transmit(s_axp192_dev, buf, sizeof(buf), 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP192 write reg 0x%02x failed: %s", reg,
                 esp_err_to_name(err));
    }
    return err;
}

static bool axp192_power_sequence(void) {
    // Step 1: enable LCD power (LDO2 @ 3300mV) and configure the two GPIOs
    // used as reset lines for the LCD (GPIO4) and touch controller (GPIO1).
    if (axp192_write_masked(0x95, 0x84, 0x72) != ESP_OK) return false;   // GPIO4 function select
    if (axp192_write_masked(0x28, 0xF0, 0xFF) != ESP_OK) return false;  // LDO2 = 3300mV (LCD power)
    if (axp192_write_masked(0x12, 0x04, 0xFF) != ESP_OK) return false;  // LDO2 enable
    if (axp192_write_masked(0x92, 0x00, 0xF8) != ESP_OK) return false;  // GPIO1 open-drain

    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 2: assert reset on both the LCD and touch controller.
    if (axp192_write_masked(0x96, 0x00, 0xFD) != ESP_OK) return false;  // GPIO4 LOW (LCD RST)
    if (axp192_write_masked(0x94, 0x00, 0xFD) != ESP_OK) return false;  // GPIO1 LOW (touch RST)

    vTaskDelay(pdMS_TO_TICKS(20));

    // Step 3: release reset on both.
    if (axp192_write_masked(0x96, 0x02, 0xFF) != ESP_OK) return false;  // GPIO4 HIGH
    if (axp192_write_masked(0x94, 0x02, 0xFF) != ESP_OK) return false;  // GPIO1 HIGH

    vTaskDelay(pdMS_TO_TICKS(50));

    // Step 4: enable backlight power (LDO3). No brightness control this
    // Alpha -- a single fixed "on" code in LDO3's voltage nibble.
    if (axp192_write_masked(0x12, 0x08, 0xFF) != ESP_OK) return false;  // LDO3 enable
    if (axp192_write_masked(0x28, 0x0F, 0xF0) != ESP_OK) return false;  // LDO3 voltage (low nibble)

    return true;
}

// ── CHSC6540 touch ──────────────────────────────────────────────────────────

static bool chsc6540_init(void) {
    static const uint8_t irq_mode_cmd[] = {0x5A, 0x5A};
    esp_err_t err = i2c_master_transmit(s_touch_dev, irq_mode_cmd,
                                        sizeof(irq_mode_cmd), 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CHSC6540 init write failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// Reads the first touch point, if any. See docs/esp/hw-reference/board-tough.md
// for the register-layout source.
static bool chsc6540_read_point(uint16_t *x, uint16_t *y) {
    static const uint8_t count_reg = 0x02;
    uint8_t buf[5] = {0};
    if (i2c_master_transmit_receive(s_touch_dev, &count_reg, 1, buf,
                                    sizeof(buf), 50) != ESP_OK) {
        return false;
    }
    if (buf[0] == 0) {
        return false;
    }
    *x = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[2]);
    *y = (uint16_t)(((buf[3] & 0x0F) << 8) | buf[4]);
    return true;
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x = 0, y = 0;
    if (chsc6540_read_point(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── LVGL display flush ──────────────────────────────────────────────────────

static bool lvgl_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx) {
    (void)panel_io;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_handle =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    // ILI9342C expects big-endian RGB565 over SPI; LVGL produces native
    // (little-endian on ESP32) byte order.
    const int32_t pixel_count = lv_area_get_width(area) * lv_area_get_height(area);
    uint16_t *pixels = (uint16_t *)px_map;
    for (int32_t i = 0; i < pixel_count; i++) {
        pixels[i] = (uint16_t)((pixels[i] >> 8) | (pixels[i] << 8));
    }

    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
}

// ── Public API ──────────────────────────────────────────────────────────────

bool platform_display_init(void) {
    ESP_LOGI(TAG, "Initializing I2C bus (AXP192 + CHSC6540)");
    i2c_master_bus_config_t bus_config = {
        .i2c_port = TOUGH_I2C_PORT,
        .sda_io_num = TOUGH_PIN_I2C_SDA,
        .scl_io_num = TOUGH_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_config, &s_i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus");
        return false;
    }

    i2c_device_config_t axp_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP192_I2C_ADDR,
        .scl_speed_hz = TOUGH_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &axp_dev_config, &s_axp192_dev) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AXP192 I2C device");
        return false;
    }

    i2c_device_config_t touch_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CHSC6540_I2C_ADDR,
        .scl_speed_hz = TOUGH_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &touch_dev_config, &s_touch_dev) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add CHSC6540 I2C device");
        return false;
    }

    ESP_LOGI(TAG, "Running AXP192 power sequence (LCD + touch reset/power)");
    if (!axp192_power_sequence()) {
        ESP_LOGE(TAG, "AXP192 power sequence failed");
        return false;
    }

    if (!chsc6540_init()) {
        ESP_LOGW(TAG, "CHSC6540 init failed; continuing (touch may not respond)");
    }

    ESP_LOGI(TAG, "Initializing SPI bus for ILI9342C panel");
    const spi_bus_config_t buscfg = {
        .mosi_io_num = TOUGH_PIN_LCD_MOSI,
        .miso_io_num = TOUGH_PIN_LCD_MISO,
        .sclk_io_num = TOUGH_PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TOUGH_LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(uint16_t),
    };
    if (spi_bus_initialize(TOUGH_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return false;
    }

    const esp_lcd_panel_io_spi_config_t io_config = ILI9341_PANEL_IO_SPI_CONFIG(
        TOUGH_PIN_LCD_CS, TOUGH_PIN_LCD_DC, NULL, NULL);
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUGH_LCD_HOST,
                                 &io_config, &s_io_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install panel IO");
        return false;
    }

    // LCD reset is driven through the AXP192 (see axp192_power_sequence),
    // not a direct ESP32 GPIO -- pass -1 so esp_lcd skips its own reset pin.
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_ili9341(s_io_handle, &panel_config, &s_panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install ILI9341-family panel driver");
        return false;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    s_hardware_ready = true;
    ESP_LOGI(TAG, "Display/touch hardware initialized");
    return true;
}

bool platform_display_register_lvgl_driver(void) {
    if (!s_hardware_ready) {
        ESP_LOGE(TAG, "Display hardware not initialized");
        return false;
    }

    s_display = lv_display_create(TOUGH_LCD_H_RES, TOUGH_LCD_V_RES);
    if (!s_display) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return false;
    }

    size_t buf_size = TOUGH_LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t);
    void *buf1 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers (%zu bytes each)", buf_size);
        return false;
    }
    lv_display_set_buffers(s_display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);
    lv_display_set_user_data(s_display, s_panel_handle);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_flush_ready_cb,
    };
    esp_lcd_panel_io_register_event_callbacks(s_io_handle, &cbs, s_display);

    s_touch_indev = lv_indev_create();
    if (!s_touch_indev) {
        ESP_LOGE(TAG, "Failed to create LVGL touch input device");
        return false;
    }
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, lvgl_touch_read_cb);

    s_lvgl_ready = true;
    ESP_LOGI(TAG, "LVGL display driver and touch input registered");
    return true;
}

bool platform_display_is_ready(void) {
    return s_hardware_ready && s_lvgl_ready;
}

// ── common/platform/platform_display.h contract ────────────────────────────
// Tough has no LVGL-driven sleep/dim state and no wired-up battery ADC this
// Alpha (see touch_ui.h). These are honest, deliberately-inert
// implementations, not stubs standing in for missing work.

bool platform_display_is_sleeping(void) {
    return false;
}

void platform_display_set_rotation(uint16_t degrees) {
    (void)degrees;  // Fixed orientation this Alpha -- no rotation support.
}

void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging) {
    (void)cfg;
    (void)is_charging;
}

bool platform_battery_is_charging(void) {
    return false;
}

int platform_battery_get_level(void) {
    return -1;  // Unavailable: AXP192 battery ADC is not wired up this Alpha.
}
