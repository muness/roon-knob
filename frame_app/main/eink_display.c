// E-ink SPI driver for 7.3" 800x480 6-color panel (Waveshare PhotoPainter)
// Adapted from PhotoPainter display_bsp.cpp — rewritten as C

#include "eink_display.h"

#include <string.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "eink";

static spi_device_handle_t s_spi;
static uint8_t *s_fb;  // Framebuffer in PSRAM

// ── Low-level GPIO + SPI ────────────────────────────────────────────────────

static void set_rst(uint8_t level) { gpio_set_level(EINK_PIN_RST, level ? 1 : 0); }
static void set_cs(uint8_t level)  { gpio_set_level(EINK_PIN_CS, level ? 1 : 0); }
static void set_dc(uint8_t level)  { gpio_set_level(EINK_PIN_DC, level ? 1 : 0); }
static int  get_busy(void)         { return gpio_get_level(EINK_PIN_BUSY); }

static void wait_busy(void) {
    int timeout_ms = 30000;
    while (!get_busy()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout_ms -= 10;
        if (timeout_ms <= 0) {
            ESP_LOGW(TAG, "wait_busy timeout!");
            return;
        }
    }
}

static void spi_write_byte(uint8_t data) {
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void send_cmd(uint8_t cmd) {
    set_dc(0);
    set_cs(0);
    spi_write_byte(cmd);
    set_cs(1);
}

static void send_data(uint8_t data) {
    set_dc(1);
    set_cs(0);
    spi_write_byte(data);
    set_cs(1);
}

// Send bulk data in 5000-byte chunks (SPI DMA-friendly)
static void send_buffer(const uint8_t *data, int len) {
    set_dc(1);
    set_cs(0);

    spi_transaction_t t = {0};
    const uint8_t *ptr = data;
    int remaining = len;

    while (remaining > 0) {
        int chunk = (remaining > 5000) ? 5000 : remaining;
        t.length = 8 * chunk;
        t.tx_buffer = ptr;
        spi_device_polling_transmit(s_spi, &t);
        ptr += chunk;
        remaining -= chunk;
    }

    set_cs(1);
}

// ── Panel commands ──────────────────────────────────────────────────────────

static void panel_reset(void) {
    set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    set_rst(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void panel_turn_on(void) {
    send_cmd(0x04);  // POWER_ON
    wait_busy();

    send_cmd(0x06);
    send_data(0x6F);
    send_data(0x1F);
    send_data(0x17);
    send_data(0x49);

    ESP_LOGI(TAG, "DISPLAY_REFRESH — waiting for panel...");
    send_cmd(0x12);  // DISPLAY_REFRESH
    send_data(0x00);
    wait_busy();
    ESP_LOGI(TAG, "Panel refresh complete");

    send_cmd(0x02);  // POWER_OFF
    send_data(0x00);
    wait_busy();
}

void eink_display_init_panel(void) {
    panel_reset();
    wait_busy();
    vTaskDelay(pdMS_TO_TICKS(50));

    send_cmd(0xAA);
    send_data(0x49); send_data(0x55); send_data(0x20);
    send_data(0x08); send_data(0x09); send_data(0x18);

    send_cmd(0x01); send_data(0x3F);
    send_cmd(0x00); send_data(0x5F); send_data(0x69);

    send_cmd(0x03);
    send_data(0x00); send_data(0x54); send_data(0x00); send_data(0x44);

    send_cmd(0x05);
    send_data(0x40); send_data(0x1F); send_data(0x1F); send_data(0x2C);

    send_cmd(0x06);
    send_data(0x6F); send_data(0x1F); send_data(0x17); send_data(0x49);

    send_cmd(0x08);
    send_data(0x6F); send_data(0x1F); send_data(0x1F); send_data(0x22);

    send_cmd(0x30); send_data(0x03);
    send_cmd(0x50); send_data(0x3F);
    send_cmd(0x60); send_data(0x02); send_data(0x00);

    // Resolution: 0x0320 = 800, 0x01E0 = 480
    send_cmd(0x61);
    send_data(0x03); send_data(0x20);
    send_data(0x01); send_data(0xE0);

    send_cmd(0x84); send_data(0x01);
    send_cmd(0xE3); send_data(0x2F);

    send_cmd(0x04);  // POWER_ON
    wait_busy();

    // Don't refresh here — let eink_ui_init() draw the boot screen
    // and do the first refresh with actual content.
    eink_display_clear(EINK_WHITE);
}

// ── Public API ──────────────────────────────────────────────────────────────

bool eink_display_init(void) {
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .miso_io_num = -1,
        .mosi_io_num = EINK_PIN_MOSI,
        .sclk_io_num = EINK_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EINK_WIDTH * EINK_HEIGHT,
    };

    spi_device_interface_config_t dev_cfg = {
        .spics_io_num = -1,  // Manual CS control
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .queue_size = 7,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = spi_bus_add_device(SPI3_HOST, &dev_cfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure output GPIOs (RST, DC, CS)
    gpio_config_t out_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << EINK_PIN_RST) | (1ULL << EINK_PIN_DC) | (1ULL << EINK_PIN_CS),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&out_cfg);

    // Configure input GPIO (BUSY)
    gpio_config_t in_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << EINK_PIN_BUSY),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in_cfg);

    set_rst(1);

    // Allocate framebuffer in PSRAM
    s_fb = heap_caps_calloc(1, EINK_FB_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%d bytes)", EINK_FB_SIZE);
        return false;
    }

    ESP_LOGI(TAG, "E-ink display initialized (800x480, 6-color, %d KB FB)", EINK_FB_SIZE / 1024);

    // Initialize panel registers and clear to white
    eink_display_init_panel();

    return true;
}

void eink_display_clear(uint8_t color) {
    if (!s_fb) return;
    memset(s_fb, (color << 4) | color, EINK_FB_SIZE);
}

void eink_display_set_pixel(uint16_t x, uint16_t y, uint8_t color) {
    if (!s_fb) return;
    if (x >= EINK_WIDTH || y >= EINK_HEIGHT) return;
    uint32_t index = (y << 8) + (y << 7) + (y << 4) + (x >> 1);
    uint8_t px = s_fb[index];
    if (x & 1) {
        s_fb[index] = (px & 0xF0) | (color & 0x0F);
    } else {
        s_fb[index] = (px & 0x0F) | (color << 4);
    }
}

uint8_t eink_display_get_pixel(uint16_t x, uint16_t y) {
    if (!s_fb) return EINK_WHITE;
    if (x >= EINK_WIDTH || y >= EINK_HEIGHT) return EINK_WHITE;
    uint32_t index = (y << 8) + (y << 7) + (y << 4) + (x >> 1);
    uint8_t px = s_fb[index];
    return (x & 1) ? (px & 0x0F) : (px >> 4);
}

uint8_t *eink_display_get_fb(void) {
    return s_fb;
}

// Rotate framebuffer 180 degrees in-place
// Each byte has two 4-bit pixels: swap nibbles and reverse byte order per row,
// then reverse row order.  Matching original EPD_Rotate180_Fast.
static void fb_rotate_180(uint8_t *buf) {
    const int bytes_per_row = EINK_WIDTH >> 1;  // 400
    for (int y = 0; y < EINK_HEIGHT / 2; y++) {
        uint8_t *top = buf + y * bytes_per_row;
        uint8_t *bot = buf + (EINK_HEIGHT - 1 - y) * bytes_per_row;
        for (int x = 0; x < bytes_per_row; x++) {
            uint8_t t = top[x];
            uint8_t b = bot[bytes_per_row - 1 - x];
            // Swap nibbles (pixel order within byte)
            top[x] = (b << 4) | (b >> 4);
            bot[bytes_per_row - 1 - x] = (t << 4) | (t >> 4);
        }
    }
    // If odd height, handle middle row
    if (EINK_HEIGHT & 1) {
        uint8_t *mid = buf + (EINK_HEIGHT / 2) * bytes_per_row;
        for (int x = 0; x < bytes_per_row / 2; x++) {
            uint8_t t = mid[x];
            uint8_t b = mid[bytes_per_row - 1 - x];
            mid[x] = (b << 4) | (b >> 4);
            mid[bytes_per_row - 1 - x] = (t << 4) | (t >> 4);
        }
    }
}

void eink_display_refresh(void) {
    if (!s_fb) return;
    ESP_LOGI(TAG, "Refreshing e-ink display...");

    // Rotate 180 into a temp buffer so set_pixel coordinates stay normal
    uint8_t *rotated = heap_caps_malloc(EINK_FB_SIZE, MALLOC_CAP_SPIRAM);
    if (rotated) {
        memcpy(rotated, s_fb, EINK_FB_SIZE);
        fb_rotate_180(rotated);
        send_cmd(0x10);
        send_buffer(rotated, EINK_FB_SIZE);
        heap_caps_free(rotated);
    } else {
        // Fallback: rotate in-place, send, rotate back
        fb_rotate_180(s_fb);
        send_cmd(0x10);
        send_buffer(s_fb, EINK_FB_SIZE);
        fb_rotate_180(s_fb);
    }

    panel_turn_on();
    ESP_LOGI(TAG, "E-ink refresh complete");
}
