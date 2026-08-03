#include "rlcd_art_dither.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 32
#define HEIGHT 24
#define BYTES (WIDTH * HEIGHT * 2)

static void set_gray(uint8_t *image, int index, uint8_t gray) {
    uint16_t r = gray >> 3;
    uint16_t g = gray >> 2;
    uint16_t pixel = (uint16_t)((r << 11) | (g << 5) | r);
    image[index * 2] = (uint8_t)pixel;
    image[index * 2 + 1] = (uint8_t)(pixel >> 8);
}

static void assert_mono(const uint8_t *image) {
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        assert((image[i * 2] == 0x00 && image[i * 2 + 1] == 0x00) ||
               (image[i * 2] == 0xff && image[i * 2 + 1] == 0xff));
    }
}

int main(void) {
    uint8_t source[BYTES];
    uint8_t output[RLCD_ART_DITHER_COUNT][BYTES];
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            set_gray(source, y * WIDTH + x,
                     (uint8_t)((x * 255) / (WIDTH - 1)));
        }
    }
    for (int mode = 0; mode < RLCD_ART_DITHER_COUNT; ++mode) {
        assert(rlcd_art_dither_rgb565(source, output[mode], WIDTH, HEIGHT,
                                      (rlcd_art_dither_mode_t)mode));
        assert_mono(output[mode]);
    }
    assert(memcmp(output[0], output[1], BYTES) != 0);
    assert(memcmp(output[1], output[2], BYTES) != 0);
    assert(memcmp(output[0], output[2], BYTES) != 0);

    memset(source, 0, sizeof(source));
    for (int mode = 0; mode < RLCD_ART_DITHER_COUNT; ++mode) {
        assert(rlcd_art_dither_rgb565(source, output[mode], WIDTH, HEIGHT,
                                      (rlcd_art_dither_mode_t)mode));
        for (int i = 0; i < BYTES; ++i) assert(output[mode][i] == 0);
    }
    puts("rlcd_art_dither_test: ok");
    return 0;
}
