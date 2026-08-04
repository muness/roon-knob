#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RLCD_ART_DITHER_BAYER4 = 0,
    RLCD_ART_DITHER_FLOYD_STEINBERG,
    RLCD_ART_DITHER_ATKINSON,
    RLCD_ART_DITHER_COUNT,
} rlcd_art_dither_mode_t;

bool rlcd_art_dither_rgb565(const uint8_t *source, uint8_t *destination,
                            int width, int height,
                            rlcd_art_dither_mode_t mode);
const char *rlcd_art_dither_label(rlcd_art_dither_mode_t mode);
