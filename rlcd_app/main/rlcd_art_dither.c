#include "rlcd_art_dither.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t s_bayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

static int luminance_at(const uint8_t *source, int index) {
    uint16_t rgb565 = (uint16_t)source[index * 2] |
                      ((uint16_t)source[index * 2 + 1] << 8);
    int r = (int)(((rgb565 >> 11) & 0x1fU) * 255U / 31U);
    int g = (int)(((rgb565 >> 5) & 0x3fU) * 255U / 63U);
    int b = (int)((rgb565 & 0x1fU) * 255U / 31U);
    return (77 * r + 150 * g + 29 * b) >> 8;
}

static void set_mono_pixel(uint8_t *destination, int index, bool white) {
    uint16_t pixel = white ? 0xffffU : 0x0000U;
    destination[index * 2] = (uint8_t)(pixel & 0xffU);
    destination[index * 2 + 1] = (uint8_t)(pixel >> 8);
}

static void dither_bayer4(const uint8_t *source, uint8_t *destination,
                          int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int threshold = s_bayer4[y & 3][x & 3] * 16 + 8;
            int index = y * width + x;
            set_mono_pixel(destination, index,
                           luminance_at(source, index) >= threshold);
        }
    }
}

static bool dither_floyd_steinberg(const uint8_t *source, uint8_t *destination,
                                   int width, int height) {
    const int stride = width + 2;
    int32_t *errors = calloc((size_t)stride * 2, sizeof(*errors));
    if (!errors) return false;
    int32_t *current = errors;
    int32_t *next = errors + stride;

    for (int y = 0; y < height; ++y) {
        bool reverse = (y & 1) != 0;
        int start = reverse ? width - 1 : 0;
        int end = reverse ? -1 : width;
        int step = reverse ? -1 : 1;
        for (int x = start; x != end; x += step) {
            int slot = x + 1;
            int index = y * width + x;
            int value = luminance_at(source, index) * 16 + current[slot];
            bool white = value >= 128 * 16;
            int error = value - (white ? 255 * 16 : 0);
            set_mono_pixel(destination, index, white);
            current[slot + step] += error * 7 / 16;
            next[slot - step] += error * 3 / 16;
            next[slot] += error * 5 / 16;
            next[slot + step] += error / 16;
        }
        int32_t *old = current;
        current = next;
        next = old;
        memset(next, 0, (size_t)stride * sizeof(*next));
    }
    free(errors);
    return true;
}

static bool dither_atkinson(const uint8_t *source, uint8_t *destination,
                            int width, int height) {
    const int stride = width + 4;
    int32_t *errors = calloc((size_t)stride * 3, sizeof(*errors));
    if (!errors) return false;
    int32_t *current = errors;
    int32_t *next = errors + stride;
    int32_t *next2 = errors + stride * 2;

    for (int y = 0; y < height; ++y) {
        bool reverse = (y & 1) != 0;
        int start = reverse ? width - 1 : 0;
        int end = reverse ? -1 : width;
        int step = reverse ? -1 : 1;
        for (int x = start; x != end; x += step) {
            int slot = x + 2;
            int index = y * width + x;
            int value = luminance_at(source, index) * 8 + current[slot];
            bool white = value >= 128 * 8;
            int share = (value - (white ? 255 * 8 : 0)) / 8;
            set_mono_pixel(destination, index, white);
            current[slot + step] += share;
            current[slot + step * 2] += share;
            next[slot - step] += share;
            next[slot] += share;
            next[slot + step] += share;
            next2[slot] += share;
        }
        int32_t *old = current;
        current = next;
        next = next2;
        next2 = old;
        memset(next2, 0, (size_t)stride * sizeof(*next2));
    }
    free(errors);
    return true;
}

bool rlcd_art_dither_rgb565(const uint8_t *source, uint8_t *destination,
                            int width, int height,
                            rlcd_art_dither_mode_t mode) {
    if (!source || !destination || width <= 0 || height <= 0) return false;
    switch (mode) {
        case RLCD_ART_DITHER_BAYER4:
            dither_bayer4(source, destination, width, height);
            return true;
        case RLCD_ART_DITHER_FLOYD_STEINBERG:
            return dither_floyd_steinberg(source, destination, width, height);
        case RLCD_ART_DITHER_ATKINSON:
            return dither_atkinson(source, destination, width, height);
        default:
            return false;
    }
}

const char *rlcd_art_dither_label(rlcd_art_dither_mode_t mode) {
    switch (mode) {
        case RLCD_ART_DITHER_BAYER4: return "A  BAYER";
        case RLCD_ART_DITHER_FLOYD_STEINBERG: return "B  FLOYD";
        case RLCD_ART_DITHER_ATKINSON: return "C  ATKINSON";
        default: return "?";
    }
}
