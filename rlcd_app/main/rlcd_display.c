#include "rlcd_display.h"
#include "rlcd_st7305_window.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

/* Waveshare's ESP-IDF sample is the hardware source for these values. */
#define RLCD_PIN_DC GPIO_NUM_5
#define RLCD_PIN_CS GPIO_NUM_40
#define RLCD_PIN_SCLK GPIO_NUM_11
#define RLCD_PIN_MOSI GPIO_NUM_12
#define RLCD_PIN_RST GPIO_NUM_41

static const char *TAG = "rlcd_display";
static esp_lcd_panel_io_handle_t s_io;
static uint8_t *s_framebuffer;
static uint8_t *s_transfer_buffer;
static int s_dirty_x1 = RLCD_WIDTH;
static int s_dirty_y1 = RLCD_HEIGHT;
static int s_dirty_x2 = -1;
static int s_dirty_y2 = -1;
static bool s_force_full_refresh;
static uint32_t s_full_refresh_count;
static uint32_t s_partial_refresh_count;

/* A fixed ordered pattern keeps artwork tonal without a large diffusion buffer
 * or an update-to-update shimmer on this 1-bit reflective panel. */
static const uint8_t s_bayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

static bool rgb565_is_white(uint16_t rgb565, uint16_t x, uint16_t y) {
    uint8_t r = (uint8_t)(((rgb565 >> 11) & 0x1FU) * 255U / 31U);
    uint8_t g = (uint8_t)(((rgb565 >> 5) & 0x3FU) * 255U / 63U);
    uint8_t b = (uint8_t)((rgb565 & 0x1FU) * 255U / 31U);
    /* Rec. 601 luminance, then a 4x4 Bayer threshold in the 0..255 range. */
    uint8_t luminance = (uint8_t)((77U * r + 150U * g + 29U * b) >> 8);
    uint8_t threshold = (uint8_t)(s_bayer4[y & 3U][x & 3U] * 16U + 8U);
    return luminance >= threshold;
}

static bool has_dirty_pixels(void) {
    return s_dirty_x2 >= s_dirty_x1 && s_dirty_y2 >= s_dirty_y1;
}

static void include_dirty_pixel(uint16_t x, uint16_t y) {
    if ((int)x < s_dirty_x1) s_dirty_x1 = x;
    if ((int)x > s_dirty_x2) s_dirty_x2 = x;
    if ((int)y < s_dirty_y1) s_dirty_y1 = y;
    if ((int)y > s_dirty_y2) s_dirty_y2 = y;
}

static void reset_dirty_bounds(void) {
    s_dirty_x1 = RLCD_WIDTH;
    s_dirty_y1 = RLCD_HEIGHT;
    s_dirty_x2 = -1;
    s_dirty_y2 = -1;
}

static void tx_data(const void *buffer, size_t length) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, -1, buffer, length));
}

static void command(uint8_t value) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, value, NULL, 0));
}
static void data(uint8_t value) { tx_data(&value, 1); }

static void reset_panel(void) {
    gpio_set_level(RLCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RLCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RLCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void init_panel(void) {
    static const uint8_t c1[] = {0x69, 0x69, 0x69, 0x69};
    static const uint8_t c2[] = {0x19, 0x19, 0x19, 0x19};
    static const uint8_t c4[] = {0x4B, 0x4B, 0x4B, 0x4B};
    static const uint8_t b3[] = {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
    static const uint8_t b4[] = {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
    reset_panel();
    command(0xD6); data(0x17); data(0x02);
    command(0xD1); data(0x01);
    command(0xC0); data(0x11); data(0x04);
    command(0xC1); tx_data(c1, sizeof(c1));
    command(0xC2); tx_data(c2, sizeof(c2));
    command(0xC4); tx_data(c4, sizeof(c4));
    command(0xC5); tx_data(c2, sizeof(c2));
    command(0xD8); data(0x80); data(0xE9);
    command(0xB2); data(0x02);
    command(0xB3); tx_data(b3, sizeof(b3));
    command(0xB4); tx_data(b4, sizeof(b4));
    command(0x62); data(0x32); data(0x03); data(0x1F);
    command(0xB7); data(0x13); command(0xB0); data(0x64);
    command(0x11); vTaskDelay(pdMS_TO_TICKS(200));
    command(0xC9); data(0x00); command(0x36); data(0x48);
    command(0x3A); data(0x11); command(0xB9); data(0x20);
    command(0xB8); data(0x29); command(0x21);
    command(0x2A); data(0x12); data(0x2A);
    command(0x2B); data(0x00); data(0xC7);
    command(0x35); data(0x00); command(0xD0); data(0xFF);
    command(0x38); command(0x29);
}

bool rlcd_display_init(void) {
    spi_bus_config_t bus = {
        .mosi_io_num = RLCD_PIN_MOSI, .miso_io_num = -1,
        .sclk_io_num = RLCD_PIN_SCLK, .quadwp_io_num = -1,
        .quadhd_io_num = -1, .max_transfer_sz = RLCD_FRAMEBUFFER_BYTES,
    };
    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = RLCD_PIN_DC, .cs_gpio_num = RLCD_PIN_CS,
        /* Waveshare's ST7305 ESP-IDF sample uses 10 MHz.  At 1 MHz a full
         * framebuffer transfer is visibly slow whenever the seek bar moves. */
        .pclk_hz = 10 * 1000 * 1000, .lcd_cmd_bits = 8,
        .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 1,
    };
    gpio_config_t output = {
        .pin_bit_mask = 1ULL << RLCD_PIN_RST, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (gpio_config(&output) != ESP_OK ||
        spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK ||
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST,
                                 &io, &s_io) != ESP_OK) {
        ESP_LOGE(TAG, "could not initialize ST7305 SPI bus");
        return false;
    }
    s_framebuffer = heap_caps_malloc(RLCD_FRAMEBUFFER_BYTES,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_transfer_buffer = heap_caps_malloc(RLCD_FRAMEBUFFER_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_framebuffer || !s_transfer_buffer) {
        ESP_LOGE(TAG, "could not allocate framebuffer and transfer buffer (%u bytes each)",
                 (unsigned)RLCD_FRAMEBUFFER_BYTES);
        return false;
    }
    init_panel();
    rlcd_display_clear(true);
    rlcd_display_refresh();
    return true;
}

bool rlcd_display_prepare_for_sleep(void) {
    if (!s_io) return false;
    /* ST7305 SLPIN stops the panel DC/DC converter, internal oscillator, and
     * panel scanning while retaining display RAM.  The datasheet requires a
     * five-millisecond command quiet period; use 100 ms so the panel has also
     * completed the documented Sleep-In transition before the S3 powers down. */
    esp_err_t err = esp_lcd_panel_io_tx_param(s_io, 0x10, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ST7305 Sleep In failed: %s", esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "ST7305 Sleep In complete");
    return true;
}

void rlcd_display_clear(bool white) {
    if (!s_framebuffer) return;
    memset(s_framebuffer, white ? 0xFF : 0x00, RLCD_FRAMEBUFFER_BYTES);
    rlcd_display_request_full_refresh();
}

void rlcd_display_set_rgb565(uint16_t x, uint16_t y, uint16_t rgb565) {
    if (!s_framebuffer || x >= RLCD_WIDTH || y >= RLCD_HEIGHT) return;
    /* ST7305 landscape packing from Waveshare's official display_bsp.cpp. */
    uint16_t inverted_y = RLCD_HEIGHT - 1 - y;
    size_t offset = (size_t)(x >> 1) * (RLCD_HEIGHT >> 2) +
                    (inverted_y >> 2);
    uint8_t bit = 7 - (((inverted_y & 3U) << 1U) | (x & 1U));
    uint8_t mask = (uint8_t)(1U << bit);
    bool white = rgb565_is_white(rgb565, x, y);
    bool was_white = (s_framebuffer[offset] & mask) != 0;
    if (white == was_white) return;
    if (white) s_framebuffer[offset] |= mask;
    else s_framebuffer[offset] &= (uint8_t)~mask;
    include_dirty_pixel(x, y);
}

void rlcd_display_request_full_refresh(void) {
    s_force_full_refresh = true;
    s_dirty_x1 = 0;
    s_dirty_y1 = 0;
    s_dirty_x2 = RLCD_WIDTH - 1;
    s_dirty_y2 = RLCD_HEIGHT - 1;
}

static void refresh_full(void) {
    command(0x2A); data(0x12); data(0x2A);
    command(0x2B); data(0x00); data(0xC7);
    command(0x2C);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, -1, s_framebuffer,
                                               RLCD_FRAMEBUFFER_BYTES));
    ++s_full_refresh_count;
    ESP_LOGI(TAG, "full refresh #%lu: %u bytes",
             (unsigned long)s_full_refresh_count,
             (unsigned)RLCD_FRAMEBUFFER_BYTES);
}

static bool refresh_partial(const rlcd_st7305_window_t *window) {
    if (!window || !s_transfer_buffer || window->transfer_size == 0 ||
        window->transfer_size > RLCD_FRAMEBUFFER_BYTES) {
        return false;
    }
    uint8_t *destination = s_transfer_buffer;
    for (int page = window->page_start; page <= window->page_end; ++page) {
        const uint8_t *source = s_framebuffer +
            (size_t)page * (RLCD_HEIGHT >> 2) +
            window->framebuffer_byte_start;
        memcpy(destination, source, window->bytes_per_page);
        destination += window->bytes_per_page;
    }
    command(0x2A); data(window->column_start); data(window->column_end);
    command(0x2B); data(window->page_start); data(window->page_end);
    command(0x2C);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, -1, s_transfer_buffer,
                                               window->transfer_size));
    ++s_partial_refresh_count;
    if (s_partial_refresh_count <= 12 || s_partial_refresh_count % 100 == 0) {
        ESP_LOGI(TAG,
                 "partial refresh #%lu: dirty=(%d,%d)-(%d,%d) col=%02x-%02x page=%u-%u bytes=%u",
                 (unsigned long)s_partial_refresh_count,
                 s_dirty_x1, s_dirty_y1, s_dirty_x2, s_dirty_y2,
                 window->column_start, window->column_end,
                 window->page_start, window->page_end,
                 (unsigned)window->transfer_size);
    }
    return true;
}

void rlcd_display_refresh(void) {
    if (!s_framebuffer || (!has_dirty_pixels() && !s_force_full_refresh)) return;
    rlcd_st7305_window_t window;
    bool have_window = rlcd_st7305_window_from_dirty(
        s_dirty_x1, s_dirty_y1, s_dirty_x2, s_dirty_y2, &window);
    if (s_force_full_refresh || !have_window ||
        window.transfer_size >= RLCD_FRAMEBUFFER_BYTES) {
        refresh_full();
    } else if (!refresh_partial(&window)) {
        refresh_full();
    }
    s_force_full_refresh = false;
    reset_dirty_bounds();
}
