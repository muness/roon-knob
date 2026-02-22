#pragma once

#include <stdint.h>
#include <stdbool.h>

// 7.3" 4-color e-ink panel: 800x480 landscape
// Mount device with long axis horizontal, USB port on right
#define EINK_WIDTH   800
#define EINK_HEIGHT  480

// 4 bits per pixel, 2 pixels per byte
#define EINK_FB_SIZE ((EINK_WIDTH * EINK_HEIGHT) / 2)  // 192000 bytes

// 4-color palette indices (7.3" panel: black, white, yellow, red)
enum {
    EINK_BLACK  = 0,
    EINK_WHITE  = 1,
    EINK_YELLOW = 2,
    EINK_RED    = 3,
    // No blue or green on this panel
    EINK_BLUE   = 0,  // Fallback to black
    EINK_GREEN  = 2,  // Fallback to yellow
};

// Pin assignments for PhotoPainter board
#define EINK_PIN_MOSI  11
#define EINK_PIN_SCLK  10
#define EINK_PIN_DC     8
#define EINK_PIN_CS     9
#define EINK_PIN_RST   12
#define EINK_PIN_BUSY  13

// Initialize SPI bus, GPIOs, and allocate framebuffer in PSRAM
bool eink_display_init(void);

// Clear framebuffer to a solid color
void eink_display_clear(uint8_t color);

// Set a single pixel in the framebuffer (no bounds check for speed)
void eink_display_set_pixel(uint16_t x, uint16_t y, uint8_t color);

// Get a single pixel from the framebuffer
uint8_t eink_display_get_pixel(uint16_t x, uint16_t y);

// Get raw framebuffer pointer (for bulk writes)
uint8_t *eink_display_get_fb(void);

// Send framebuffer to panel and trigger full refresh (~1.5s)
void eink_display_refresh(void);

// Initialize panel registers (called by eink_display_init)
void eink_display_init_panel(void);
