#pragma once

#include <stdint.h>

// Simple bitmap font for e-ink rendering (ASCII only, multiple sizes)
// Each glyph is stored as a 1-bit-per-pixel bitmap, MSB first per byte.

typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t first_char;   // First ASCII code (usually 32 = space)
    uint8_t last_char;    // Last ASCII code (usually 126 = ~)
    const uint8_t *data;  // Packed 1bpp bitmaps, row-major, ((width+7)/8)*height bytes per glyph
} eink_font_t;

// Built-in fonts
extern const eink_font_t eink_font_16;   // 8x16 normal text
extern const eink_font_t eink_font_24;   // 12x24 medium text
extern const eink_font_t eink_font_32;   // 16x32 large text (track title)

// Draw a string at (x,y) in the e-ink framebuffer
// fg_color/bg_color: palette indices (use 0xFF for bg to skip background pixels)
void eink_font_draw_string(uint16_t x, uint16_t y, const char *str,
                           const eink_font_t *font,
                           uint8_t fg_color, uint8_t bg_color);

// Measure string width in pixels
int eink_font_string_width(const char *str, const eink_font_t *font);
