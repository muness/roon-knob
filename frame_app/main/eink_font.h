#pragma once

#include <stdbool.h>
#include <stdint.h>

// Unicode bitmap font for e-ink rendering, scaled to multiple sizes.

typedef struct {
    uint8_t width;
    uint8_t height;
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

// Measure one Unicode code point, including fallback-glyph behavior.
int eink_font_codepoint_width(uint32_t codepoint, const eink_font_t *font);

bool eink_font_has_glyph(uint32_t codepoint);
