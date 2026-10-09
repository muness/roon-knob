/* HALO TOUCH V2 display platform: ST77916 over QSPI + LVGL 9 + CST816 touch.
 * Adapted from idf_app/main/platform_display_idf.c (SH8601). Differences: always on (no sleep/dim),
 * different panel/pins, touch on the shared new-API I2C master bus, no 2-px rounder (ST77916 does not
 * need it), flush completion from the panel's color-transfer-done callback. */
#include "platform_display_halo.h"
#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "bridge_client.h"
#include "ui.h"
#include "halo_st77916_init.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "lvgl.h"

static const char *TAG = "display";

/* ---- board pins (HALO TOUCH V2, from the factory firmware) ---- */
#define LCD_HOST        SPI2_HOST
#define LCD_H_RES       360
#define LCD_V_RES       360
#define PIN_LCD_CS      ((gpio_num_t)21)
#define PIN_LCD_SCK     ((gpio_num_t)47)
#define PIN_LCD_D0      ((gpio_num_t)14)
#define PIN_LCD_D1      ((gpio_num_t)13)
#define PIN_LCD_D2      ((gpio_num_t)12)
#define PIN_LCD_D3      ((gpio_num_t)11)
#define PIN_LCD_RST     ((gpio_num_t)10)
#define PIN_BACKLIGHT   ((gpio_num_t)46)
#define PIN_I2C_SDA     ((gpio_num_t)48)
#define PIN_I2C_SCL     ((gpio_num_t)45)
#define PIN_TOUCH_RST   ((gpio_num_t)9)
#define TOUCH_I2C_ADDR  0x15
#define I2C_FREQ_HZ     400000

/* Factory runs the QSPI link at 80 MHz; start conservatively. */
#define HALO_LCD_PCLK_HZ (40 * 1000 * 1000)
/* Touch axes relative to the panel; flip here if the unit reports mirrored coordinates. */
#define HALO_TOUCH_SWAP_XY 0
#define HALO_TOUCH_INVERT_X 0
#define HALO_TOUCH_INVERT_Y 0
/* 1: lv_display_flush_ready() from the panel's color-trans-done callback (correct buffer ownership).
 * 0: Dial behaviour (flush_ready right after queueing the transfer). */
#define HALO_FLUSH_FROM_DONE_CB 1

#define LVGL_BUF_HEIGHT 24
#define LVGL_TICK_PERIOD_MS 2
#define ROTATE_BUF_ROWS 60
#define ROTATE_BUF_SIZE (LCD_H_RES * ROTATE_BUF_ROWS * sizeof(uint16_t))

/* Backlight: factory 25 kHz, 7-bit. */
#define BL_TIMER_BITS LEDC_TIMER_7_BIT
#define BL_FREQ_HZ 25000
#define BL_MAX_DUTY ((1 << 7) - 1)

/* Gesture thresholds (same semantics as the Dial). */
#define SWIPE_MIN_DISTANCE 60
#define SWIPE_MAX_TIME_MS 500
#define DOUBLE_TAP_MAX_MS 400
#define DOUBLE_TAP_MAX_DISTANCE 40

static lv_display_t *s_display;
static lv_indev_t *s_touch_indev;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_touch_dev;
static esp_timer_handle_t s_tick_timer;
static uint8_t *s_rotate_buf;
static bool s_hardware_ready, s_lvgl_ready;
static uint16_t s_rotation;

/* Gesture / art-mode state (art mode = controls hidden, artwork only). */
static int16_t s_start_x, s_start_y, s_last_x, s_last_y;
static int64_t s_start_ms;
static bool s_tracking, s_touch_swallowed;
static int64_t s_last_tap_ms;
static int16_t s_last_tap_x, s_last_tap_y;
static bool s_art_mode;
static volatile bool s_pending_enter_art, s_pending_exit_art;

/* ---------------- backlight ---------------- */
void platform_display_set_brightness(int percent) {
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * BL_MAX_DUTY / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void backlight_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_TIMER_BITS,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {
        .gpio_num = PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = BL_MAX_DUTY, /* always on, full brightness */
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

/* ---------------- touch (CST816 @0x15) ---------------- */
static bool touch_init(void) {
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_TOUCH_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus, &s_i2c_bus) != ESP_OK) return false;
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = TOUCH_I2C_ADDR,
                               .scl_speed_hz = I2C_FREQ_HZ};
    if (i2c_master_bus_add_device(s_i2c_bus, &dev, &s_touch_dev) != ESP_OK) return false;
    uint8_t reg = 0xA7, id[3] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_touch_dev, &reg, 1, id, sizeof id, 50);
    ESP_LOGI(TAG, "CST816 id %02X %02X %02X (%s)", id[0], id[1], id[2], esp_err_to_name(err));
    uint8_t no_autosleep[2] = {0xFE, 0x01}; /* DisAutoSleep: keep the controller answering */
    i2c_master_transmit(s_touch_dev, no_autosleep, sizeof no_autosleep, 50);
    return err == ESP_OK;
}

static bool touch_read(int16_t *x, int16_t *y) {
    if (!s_touch_dev) return false;
    uint8_t reg = 0x02, d[5];
    if (i2c_master_transmit_receive(s_touch_dev, &reg, 1, d, sizeof d, 20) != ESP_OK) return false;
    if ((d[0] & 0x0F) == 0) return false;
    int px = ((d[1] & 0x0F) << 8) | d[2];
    int py = ((d[3] & 0x0F) << 8) | d[4];
    if (HALO_TOUCH_SWAP_XY) { int t = px; px = py; py = t; }
    if (HALO_TOUCH_INVERT_X) px = LCD_H_RES - 1 - px;
    if (HALO_TOUCH_INVERT_Y) py = LCD_V_RES - 1 - py;
    *x = (int16_t)px;
    *y = (int16_t)py;
    return true;
}

/* Same gesture semantics as the Dial: swipe up / double tap -> art mode, swipe down -> exit.
 * Any touch while in art mode exits it and is not delivered to widgets. */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    int16_t x, y;
    if (touch_read(&x, &y)) {
        if (!s_tracking) {
            s_start_x = x; s_start_y = y;
            s_start_ms = esp_timer_get_time() / 1000;
            s_tracking = true;
            if (s_art_mode) { s_pending_exit_art = true; s_touch_swallowed = true; }
        }
        s_last_x = x; s_last_y = y;
        data->point.x = x;
        data->point.y = y;
        data->state = s_touch_swallowed ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
        return;
    }

    data->point.x = s_last_x;
    data->point.y = s_last_y;
    data->state = LV_INDEV_STATE_RELEASED;
    if (!s_tracking) return;
    s_tracking = false;
    if (s_touch_swallowed) { s_touch_swallowed = false; return; }

    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - s_start_ms >= SWIPE_MAX_TIME_MS) return;
    int16_t dx = s_last_x - s_start_x, dy = s_last_y - s_start_y;
    if (s_rotation == 180) { dx = -dx; dy = -dy; }

    if (dy < -SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
        if (bridge_client_is_ready_for_art_mode()) {
            ESP_LOGI(TAG, "Swipe up - art mode");
            s_pending_enter_art = true;
        }
    } else if (dy > SWIPE_MIN_DISTANCE && abs(dy) > abs(dx)) {
        ESP_LOGI(TAG, "Swipe down - exit art mode");
        s_pending_exit_art = true;
    } else if (abs(dx) < DOUBLE_TAP_MAX_DISTANCE && abs(dy) < DOUBLE_TAP_MAX_DISTANCE) {
        if (now_ms - s_last_tap_ms < DOUBLE_TAP_MAX_MS &&
            abs(s_last_x - s_last_tap_x) < DOUBLE_TAP_MAX_DISTANCE &&
            abs(s_last_y - s_last_tap_y) < DOUBLE_TAP_MAX_DISTANCE) {
            if (bridge_client_is_ready_for_art_mode()) {
                ESP_LOGI(TAG, "Double tap - art mode");
                s_pending_enter_art = true;
            }
            s_last_tap_ms = 0;
        } else {
            s_last_tap_ms = now_ms;
            s_last_tap_x = s_last_x;
            s_last_tap_y = s_last_y;
        }
    }
}

void platform_display_process_pending(void) {
    if (s_pending_enter_art) {
        s_pending_enter_art = false;
        if (!s_art_mode) { s_art_mode = true; ui_set_controls_visible(false); }
    }
    if (s_pending_exit_art) {
        s_pending_exit_art = false;
        if (s_art_mode) { s_art_mode = false; ui_set_controls_visible(true); }
    }
}

/* ---------------- LVGL flush ---------------- */
static void rotate180_rgb565(const uint16_t *src, uint16_t *dst, int n) {
    for (int i = 0; i < n; i++) dst[n - 1 - i] = src[i];
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    const int n = w * h;
    int32_t x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;

    if (lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_180 && s_rotate_buf && n <= LCD_H_RES * ROTATE_BUF_ROWS) {
        rotate180_rgb565((const uint16_t *)px_map, (uint16_t *)s_rotate_buf, n);
        memcpy(px_map, s_rotate_buf, n * sizeof(uint16_t));
        x1 = LCD_H_RES - 1 - area->x2; x2 = LCD_H_RES - 1 - area->x1;
        y1 = LCD_V_RES - 1 - area->y2; y2 = LCD_V_RES - 1 - area->y1;
    }
    /* Panel wants big-endian RGB565. */
    uint16_t *p = (uint16_t *)px_map;
    for (int i = 0; i < n; i++) p[i] = (uint16_t)((p[i] >> 8) | (p[i] << 8));

    esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2 + 1, y2 + 1, px_map);
#if !HALO_FLUSH_FROM_DONE_CB
    lv_display_flush_ready(disp);
#endif
}

#if HALO_FLUSH_FROM_DONE_CB
static bool IRAM_ATTR color_trans_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx) {
    (void)io; (void)edata;
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}
#endif

static void lvgl_tick_cb(void *arg) { (void)arg; lv_tick_inc(LVGL_TICK_PERIOD_MS); }

/* ---------------- init ---------------- */
bool platform_display_init(void) {
    ESP_LOGI(TAG, "Initializing HALO display hardware");
    backlight_init();

    const spi_bus_config_t buscfg = ST77916_PANEL_BUS_QSPI_CONFIG(PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2,
                                                                  PIN_LCD_D3, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* Done-callback user_ctx is the lv_display, which exists only after register_lvgl_driver(); the callback
     * is attached then via esp_lcd_panel_io_register_event_callbacks(). */
    esp_lcd_panel_io_spi_config_t io_cfg = ST77916_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    io_cfg.pclk_hz = HALO_LCD_PCLK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    st77916_vendor_config_t vendor = {
        .init_cmds = halo_v2_st77916_init,
        .init_cmds_size = sizeof(halo_v2_st77916_init) / sizeof(halo_v2_st77916_init[0]),
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(s_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    if (!touch_init()) ESP_LOGW(TAG, "CST816 not responding; touch disabled");

    s_hardware_ready = true;
    ESP_LOGI(TAG, "HALO display hardware ready (QSPI %d MHz)", HALO_LCD_PCLK_HZ / 1000000);
    return true;
}

bool platform_display_register_lvgl_driver(void) {
    if (!s_hardware_ready) { ESP_LOGE(TAG, "Display hardware not initialized"); return false; }

    s_display = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!s_display) return false;

    size_t buf_size = LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t);
    void *buf1 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) { ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers"); return false; }
    ESP_LOGI(TAG, "LVGL draw buffers: 2 x %zu bytes (internal DMA)", buf_size);
    lv_display_set_buffers(s_display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);

#if HALO_FLUSH_FROM_DONE_CB
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = color_trans_done_cb};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, s_display));
#endif

    s_rotate_buf = heap_caps_malloc(ROTATE_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_rotate_buf) ESP_LOGW(TAG, "No rotation buffer; 180 degree rotation disabled");

    s_touch_indev = lv_indev_create();
    if (!s_touch_indev) return false;
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, touch_read_cb);

    const esp_timer_create_args_t targs = {.callback = lvgl_tick_cb, .name = "lvgl_tick"};
    if (esp_timer_create(&targs, &s_tick_timer) != ESP_OK ||
        esp_timer_start_periodic(s_tick_timer, LVGL_TICK_PERIOD_MS * 1000ULL) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL tick timer failed");
        return false;
    }
    s_lvgl_ready = true;
    ESP_LOGI(TAG, "LVGL display + touch registered");
    return true;
}

bool platform_display_is_ready(void) { return s_hardware_ready && s_lvgl_ready; }

i2c_master_bus_handle_t platform_display_halo_i2c_bus(void) { return s_i2c_bus; }

/* ---------------- platform_display.h / platform_power.h (always on, USB powered) ---------------- */
bool platform_display_is_sleeping(void) { return false; }

void platform_display_set_rotation(uint16_t degrees) {
    if (!s_display) return;
    if (degrees == 180) { s_rotation = 180; lv_display_set_rotation(s_display, LV_DISPLAY_ROTATION_180); }
    else {
        if (degrees != 0) ESP_LOGW(TAG, "Rotation %u unsupported (0/180 only), using 0", degrees);
        s_rotation = 0;
        lv_display_set_rotation(s_display, LV_DISPLAY_ROTATION_0);
    }
}

void platform_display_apply_config(const rk_cfg_t *cfg, bool charging) { (void)cfg; (void)charging; }

void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) return;
    out->battery_level = -1;
    out->source = PLATFORM_POWER_SOURCE_EXTERNAL;
    out->external_power = true; /* USB powered, never on battery */
}
void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out) { (void)out; }
bool platform_power_debug_arm_sleep(uint32_t delay_sec) { (void)delay_sec; return false; }
