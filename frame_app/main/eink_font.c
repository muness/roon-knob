// UTF-8 bitmap text rendering for the Frame e-ink display.

#include "eink_font.h"
#include "eink_display.h"
#include "controller_utf8.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t codepoint;
    uint32_t bitmap_offset;
    uint8_t width;
} unifont_glyph_t;

#include "eink_font_unifont.inc"

const eink_font_t eink_font_16 = {.width = 8, .height = 16};
const eink_font_t eink_font_24 = {.width = 12, .height = 24};
const eink_font_t eink_font_32 = {.width = 16, .height = 32};

static bool is_combining(uint32_t codepoint) {
    return codepoint >= 0x0300 && codepoint <= 0x036F;
}

static const unifont_glyph_t *find_glyph(uint32_t codepoint) {
    size_t low = 0;
    size_t high = UNIFONT_GLYPH_COUNT;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        const uint32_t candidate = s_unifont_glyphs[middle].codepoint;
        if (candidate < codepoint) low = middle + 1;
        else high = middle;
    }
    if (low < UNIFONT_GLYPH_COUNT &&
        s_unifont_glyphs[low].codepoint == codepoint) {
        return &s_unifont_glyphs[low];
    }
    return NULL;
}

static const unifont_glyph_t *glyph_or_replacement(uint32_t codepoint) {
    const unifont_glyph_t *glyph = find_glyph(codepoint);
    return glyph ? glyph : find_glyph(0xFFFD);
}

bool eink_font_has_glyph(uint32_t codepoint) {
    return find_glyph(codepoint) != NULL;
}

static int scaled_width(const unifont_glyph_t *glyph,
                        const eink_font_t *font) {
    return glyph ? glyph->width * font->width / 8 : font->width;
}

static void draw_glyph_scaled(uint16_t x, uint16_t y,
                              const unifont_glyph_t *glyph,
                              const eink_font_t *font,
                              uint8_t fg, uint8_t bg) {
    if (!glyph) return;
    const int target_width = scaled_width(glyph, font);
    const int source_bytes_per_row = glyph->width / 8;
    const uint8_t *bitmap = s_unifont_bitmap + glyph->bitmap_offset;

    for (int target_y = 0; target_y < font->height; ++target_y) {
        const int source_y = target_y * 16 / font->height;
        for (int target_x = 0; target_x < target_width; ++target_x) {
            const int source_x = target_x * glyph->width / target_width;
            const uint8_t bits =
                bitmap[source_y * source_bytes_per_row + source_x / 8];
            const uint8_t color =
                (bits & (0x80u >> (source_x % 8))) ? fg : bg;
            if (color == 0xFF) continue;
            const uint16_t px = x + target_x;
            const uint16_t py = y + target_y;
            if (px < EINK_WIDTH && py < EINK_HEIGHT) {
                eink_display_set_pixel(px, py, color);
            }
        }
    }
}

void eink_font_draw_string(uint16_t x, uint16_t y, const char *str,
                           const eink_font_t *font,
                           uint8_t fg_color, uint8_t bg_color) {
    if (!str || !font) return;
    const char *cursor = str;
    uint16_t current_x = x;
    uint16_t previous_x = x;
    while (*cursor) {
        const uint32_t codepoint = controller_utf8_decode_next(&cursor);
        const unifont_glyph_t *glyph = glyph_or_replacement(codepoint);
        const bool combining = is_combining(codepoint);
        const int advance = combining ? 0 : scaled_width(glyph, font);
        const uint16_t glyph_x = combining ? previous_x : current_x;
        if (glyph_x + scaled_width(glyph, font) > EINK_WIDTH) break;
        // Combining marks overlay the preceding glyph; a transparent
        // background preserves the base letter underneath the accent.
        draw_glyph_scaled(glyph_x, y, glyph, font, fg_color,
                          combining ? 0xFF : bg_color);
        if (advance > 0) {
            previous_x = current_x;
            current_x += advance;
        }
    }
}

int eink_font_codepoint_width(uint32_t codepoint, const eink_font_t *font) {
    if (!font || is_combining(codepoint)) return 0;
    return scaled_width(glyph_or_replacement(codepoint), font);
}

int eink_font_string_width(const char *str, const eink_font_t *font) {
    if (!str || !font) return 0;
    int width = 0;
    const char *cursor = str;
    while (*cursor) {
        width += eink_font_codepoint_width(
            controller_utf8_decode_next(&cursor), font);
    }
    return width;
}
