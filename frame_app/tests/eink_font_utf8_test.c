#include "eink_font.h"

#include <assert.h>
#include <stdio.h>

static int s_pixels_drawn;

void eink_display_set_pixel(uint16_t x, uint16_t y, uint8_t color) {
    (void)x;
    (void)y;
    (void)color;
    ++s_pixels_drawn;
}

int main(void) {
    assert(eink_font_has_glyph(0x00E9));  // é
    assert(eink_font_has_glyph(0x2605));  // ★
    assert(!eink_font_has_glyph(0x1F3B5));  // musical note emoji

    assert(eink_font_string_width("Beyonc\xC3\xA9", &eink_font_16) == 56);
    assert(eink_font_string_width("A\xE2\x98\x85" "B", &eink_font_16) == 32);
    assert(eink_font_string_width("e\xCC\x81", &eink_font_16) == 8);

    eink_font_draw_string(0, 0, "Beyonc\xC3\xA9 \xE2\x98\x85",
                          &eink_font_16, 0, 0xFF);
    assert(s_pixels_drawn > 0);
    puts("eink_font_utf8_test: ok");
    return 0;
}
