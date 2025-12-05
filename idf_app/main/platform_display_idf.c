#include "platform_display_idf.h"
#include "platform/platform_display.h"
#include "display_sleep.h"
#include "i2c_bsp.h"
#include "lcd_touch_bsp.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lcd_sh8601.h"

static const char *TAG = "display";

// Swipe gesture detection
#define SWIPE_MIN_DISTANCE 60   // Minimum pixels for swipe
#define SWIPE_MAX_TIME_MS 500   // Maximum time for swipe gesture
static int16_t s_touch_start_x = 0;
static int16_t s_touch_start_y = 0;
static int64_t s_touch_start_time = 0;
static bool s_touch_tracking = false;
static volatile bool s_pending_art_mode = false;   // Deferred art mode activation
static volatile bool s_pending_exit_art_mode = false;  // Deferred art mode exit

// LVGL tick timer (critical for LVGL to know time is passing)
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
#define LVGL_TICK_PERIOD_MS 2

// Display configuration - matches hardware pinout
#define LCD_HOST SPI2_HOST
#define LCD_H_RES 360
#define LCD_V_RES 360
#define LVGL_BUF_HEIGHT (LCD_V_RES / 10)
#define PIN_NUM_LCD_CS      ((gpio_num_t)14)
#define PIN_NUM_LCD_PCLK    ((gpio_num_t)13)
#define PIN_NUM_LCD_DATA0   ((gpio_num_t)15)
#define PIN_NUM_LCD_DATA1   ((gpio_num_t)16)
#define PIN_NUM_LCD_DATA2   ((gpio_num_t)17)
#define PIN_NUM_LCD_DATA3   ((gpio_num_t)18)
#define PIN_NUM_LCD_RST     ((gpio_num_t)21)
#define PIN_NUM_BK_LIGHT    ((gpio_num_t)47)

// LCD initialization commands for SH8601 (from reference example)
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0},
    {0xF2, (uint8_t[]){0x28}, 1, 0},
    {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0},
    {0x83, (uint8_t[]){0xE0}, 1, 0},
    {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0},
    {0xB0, (uint8_t[]){0x56}, 1, 0},
    {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0},
    {0xB4, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0},
    {0xB7, (uint8_t[]){0x40}, 1, 0},
    {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x08}, 1, 0},
    {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x80}, 1, 0},
    {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0},
    {0xC3, (uint8_t[]){0x80}, 1, 0},
    {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0},
    {0xC6, (uint8_t[]){0xA9}, 1, 0},
    {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0},
    {0xC9, (uint8_t[]){0xA9}, 1, 0},
    {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0},
    {0xD0, (uint8_t[]){0x91}, 1, 0},
    {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0},
    {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0},
    {0xF1, (uint8_t[]){0x10}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0},
    {0xF3, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0},
    {0xE2, (uint8_t[]){0x00}, 1, 0},
    {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0},
    {0xE5, (uint8_t[]){0x06}, 1, 0},
    {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0},
    {0xE8, (uint8_t[]){0x05}, 1, 0},
    {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0},
    {0xEB, (uint8_t[]){0x00}, 1, 0},
    {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0},
    {0xEE, (uint8_t[]){0x00}, 1, 0},
    {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0},
    {0xF9, (uint8_t[]){0x00}, 1, 0},
    {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0},
    {0xFC, (uint8_t[]){0x00}, 1, 0},
    {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0},
    {0x62, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0},
    {0x65, (uint8_t[]){0x00}, 1, 0},
    {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0},
    {0x68, (uint8_t[]){0x00}, 1, 0},
    {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0},
    {0x6B, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0},
    {0x72, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0},
    {0x75, (uint8_t[]){0x00}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0},
    {0x78, (uint8_t[]){0x00}, 1, 0},
    {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0},
    {0x7B, (uint8_t[]){0x00}, 1, 0},
    {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0},
    {0x82, (uint8_t[]){0x06}, 1, 0},
    {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0},
    {0x85, (uint8_t[]){0x04}, 1, 0},
    {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0},
    {0x88, (uint8_t[]){0x48}, 1, 0},
    {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0},
    {0x8B, (uint8_t[]){0x02}, 1, 0},
    {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0},
    {0x8E, (uint8_t[]){0x00}, 1, 0},
    {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0},
    {0x91, (uint8_t[]){0x00}, 1, 0},
    {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0},
    {0x94, (uint8_t[]){0xDA}, 1, 0},
    {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0},
    {0x97, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0},
    {0x9A, (uint8_t[]){0x0C}, 1, 0},
    {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0},
    {0x9D, (uint8_t[]){0x04}, 1, 0},
    {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0},
    {0xA0, (uint8_t[]){0x48}, 1, 0},
    {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0},
    {0xA3, (uint8_t[]){0x02}, 1, 0},
    {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0},
    {0xA6, (uint8_t[]){0x00}, 1, 0},
    {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0},
    {0xA9, (uint8_t[]){0x00}, 1, 0},
    {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0},
    {0xAC, (uint8_t[]){0xD7}, 1, 0},
    {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0},
    {0xAF, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x09}, 1, 0},
    {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0},
    {0xB5, (uint8_t[]){0x04}, 1, 0},
    {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0},
    {0xB8, (uint8_t[]){0x48}, 1, 0},
    {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0},
    {0xBB, (uint8_t[]){0x02}, 1, 0},
    {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0},
    {0xC1, (uint8_t[]){0x47}, 1, 0},
    {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0},
    {0xC4, (uint8_t[]){0x74}, 1, 0},
    {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0},
    {0xC7, (uint8_t[]){0x01}, 1, 0},
    {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0},
    {0xD0, (uint8_t[]){0x10}, 1, 0},
    {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0},
    {0xD3, (uint8_t[]){0x65}, 1, 0},
    {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0},
    {0xD6, (uint8_t[]){0x99}, 1, 0},
    {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0},
    {0xD9, (uint8_t[]){0xAA}, 1, 0},
    {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0x21, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
};

static lv_display_t *s_display = NULL;
static lv_indev_t *s_touch_indev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static bool s_hardware_ready = false;
static bool s_lvgl_ready = false;

// Callback when flush is ready
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    (void)panel_io;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// Rounder callback for SH8601 display (requires 2-pixel alignment)
static void lvgl_rounder_cb(lv_event_t *e) {
    lv_area_t *area = lv_event_get_param(e);

    // Round the start of coordinate down to the nearest 2M number
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    // Round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

// LVGL flush callback
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    // Swap bytes for big-endian QSPI display (SH8601 expects big-endian RGB565)
    // ESP32 is little-endian, so we need to swap each 16-bit pixel
    const int width = offsetx2 - offsetx1 + 1;
    const int height = offsety2 - offsety1 + 1;
    const int pixel_count = width * height;
    uint16_t *pixels = (uint16_t *)px_map;
    for (int i = 0; i < pixel_count; i++) {
        pixels[i] = (pixels[i] >> 8) | (pixels[i] << 8);
    }

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);

    // MUST call flush_ready here - the notify callback doesn't work properly with LVGL 9.x
    lv_display_flush_ready(disp);
}

// LVGL tick timer callback - critical for LVGL to track time
static void lvgl_tick_timer_cb(void *arg) {
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// LVGL touch read callback with swipe gesture detection
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x, y;

    if (tpGetCoordinates(&x, &y)) {
        display_activity_detected();  // Wake display and reset sleep timers
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;

        // Start tracking touch for swipe detection
        if (!s_touch_tracking) {
            s_touch_start_x = x;
            s_touch_start_y = y;
            s_touch_start_time = esp_timer_get_time() / 1000;  // Convert to ms
            s_touch_tracking = true;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;

        // Check for swipe gesture on release
        if (s_touch_tracking) {
            int64_t elapsed = (esp_timer_get_time() / 1000) - s_touch_start_time;

            if (elapsed < SWIPE_MAX_TIME_MS) {
                int16_t dx = data->point.x - s_touch_start_x;
                int16_t dy = data->point.y - s_touch_start_y;

                // Check for swipe up (negative Y direction) - enter art mode
                if (dy < -SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
                    ESP_LOGI(TAG, "Swipe up detected - queueing art mode");
                    s_pending_art_mode = true;  // Defer to avoid LVGL threading issues
                }
                // Check for swipe down (positive Y direction) - exit art mode
                else if (dy > SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
                    ESP_LOGI(TAG, "Swipe down detected - queueing exit art mode");
                    s_pending_exit_art_mode = true;  // Defer to avoid LVGL threading issues
                }
            }
            s_touch_tracking = false;
        }
    }
}

bool platform_display_init(void) {
    ESP_LOGI(TAG, "Initializing display hardware");

    // Initialize backlight with PWM at reduced brightness (50%)
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .gpio_num = PIN_NUM_BK_LIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 16,  // 6.25% brightness (0-255) - very dim
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Initialize SPI bus
    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .data0_io_num = PIN_NUM_LCD_DATA0,
        .data1_io_num = PIN_NUM_LCD_DATA1,
        .sclk_io_num = PIN_NUM_LCD_PCLK,
        .data2_io_num = PIN_NUM_LCD_DATA2,
        .data3_io_num = PIN_NUM_LCD_DATA3,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Install panel IO
    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
        PIN_NUM_LCD_CS,
        NULL,  // No callback - we call flush_ready in flush_cb
        NULL
    );

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io_handle));

    // Install LCD driver
    ESP_LOGI(TAG, "Install SH8601 panel driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,  // Match smart-knob reference
        .bits_per_pixel = 16,  // RGB565
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(s_io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));

    // Initialize I2C bus and touch controller
    ESP_LOGI(TAG, "Initializing I2C bus");
    i2c_master_Init();

    ESP_LOGI(TAG, "Initializing CST816 touch controller");
    lcd_touch_init();
    ESP_LOGI(TAG, "Touch controller initialized successfully");

    s_hardware_ready = true;
    ESP_LOGI(TAG, "Display hardware initialized successfully");
    return true;
}

bool platform_display_register_lvgl_driver(void) {
    if (!s_hardware_ready) {
        ESP_LOGE(TAG, "Display hardware not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Registering LVGL display driver");

    // Create LVGL display
    s_display = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!s_display) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return false;
    }

    // Allocate and clear draw buffers (zero-initialize to prevent garbage/pink display)
    size_t buf_size = LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t);
    void *buf1 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        return false;
    }
    ESP_LOGI(TAG, "Allocated %zu bytes for each draw buffer", buf_size);

    lv_display_set_buffers(s_display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);
    lv_display_set_user_data(s_display, s_panel_handle);

    // Register rounder callback for 2-pixel alignment requirement
    lv_display_add_event_cb(s_display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    // Register touch input device
    ESP_LOGI(TAG, "Registering LVGL touch input device");
    s_touch_indev = lv_indev_create();
    if (!s_touch_indev) {
        ESP_LOGE(TAG, "Failed to create LVGL touch input device");
        return false;
    }
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, lvgl_touch_read_cb);

    // Create LVGL tick timer - CRITICAL for LVGL to know time is passing
    ESP_LOGI(TAG, "Creating LVGL tick timer (%dms period)", LVGL_TICK_PERIOD_MS);
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = lvgl_tick_timer_cb,
        .name = "lvgl_tick"
    };
    esp_err_t timer_err = esp_timer_create(&lvgl_tick_timer_args, &s_lvgl_tick_timer);
    if (timer_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer: %s", esp_err_to_name(timer_err));
        return false;
    }
    timer_err = esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000ULL);
    if (timer_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer: %s", esp_err_to_name(timer_err));
        return false;
    }
    ESP_LOGI(TAG, "LVGL tick timer started successfully");

    // Note: LVGL timer_handler will be called by ui_loop_iter()
    // No separate LVGL task needed since ui_loop handles it

    s_lvgl_ready = true;
    ESP_LOGI(TAG, "LVGL display driver and touch input registered successfully");
    return true;
}

bool platform_display_is_ready(void) {
    return s_hardware_ready && s_lvgl_ready;
}

void platform_display_init_sleep(TaskHandle_t lvgl_task_handle) {
    if (s_panel_handle == NULL) {
        ESP_LOGW(TAG, "Cannot init display sleep - panel not initialized");
        return;
    }
    display_sleep_init(s_panel_handle, lvgl_task_handle);
}

bool platform_display_is_sleeping(void) {
    return display_is_sleeping();
}

void platform_display_process_pending(void) {
    // Process deferred swipe gesture art mode
    if (s_pending_art_mode) {
        s_pending_art_mode = false;
        display_art_mode();
    }
    // Process deferred exit art mode (swipe down)
    if (s_pending_exit_art_mode) {
        s_pending_exit_art_mode = false;
        // Only exit if in art mode - use display_wake which handles state properly
        if (display_get_state() == DISPLAY_STATE_ART_MODE) {
            display_wake();  // Returns to normal state with controls visible
        }
    }
    // Process deferred timer-triggered state changes
    display_process_pending();
}
