#pragma once

#include <stdint.h>

// Floyd-Steinberg dither RGB888 image to 6-color e-ink palette.
// src: input RGB888 (w*h*3 bytes, R,G,B order)
// dst: output RGB888 with each pixel snapped to nearest palette color
// The caller then maps each output pixel to a palette index.
void eink_dither_rgb888(const uint8_t *src, uint8_t *dst, int w, int h);

// Map RGB888 pixel to nearest 6-color palette index (0-6)
int eink_nearest_color(uint8_t r, uint8_t g, uint8_t b);

// Scale RGB888 image using bilinear interpolation (fixed-point)
void eink_scale_bilinear(const uint8_t *src, int src_w, int src_h,
                         uint8_t *dst, int dst_w, int dst_h);

// Convert RGB565 buffer to RGB888 buffer
// src: RGB565 data (w*h*2 bytes, little-endian)
// dst: RGB888 data (w*h*3 bytes)
void eink_rgb565_to_rgb888(const uint8_t *src, uint8_t *dst, int w, int h);
