// Floyd-Steinberg dithering for 6-color ACeP e-ink palette
// Panel supports: Black, White, Yellow, Red, Blue, Green
// Adapted from PhotoPainter imgdecode_app.cpp — rewritten as C

#include "eink_dither.h"

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

// 6-color ACeP e-ink palette (RGB888)
#define PALETTE_SIZE 6
static const uint8_t PALETTE[PALETTE_SIZE][3] = {
    {0,   0,   0  },  // 0 = Black
    {255, 255, 255},  // 1 = White
    {255, 255, 0  },  // 2 = Yellow
    {255, 0,   0  },  // 3 = Red
    {0,   0,   255},  // 4 = Blue
    {0,   255, 0  },  // 5 = Green
};

// Maps palette array index → panel hardware color index
// Panel indices: Black=0, White=1, Yellow=2, Red=3, Blue=5, Green=6 (4 unused)
static const uint8_t PALETTE_PANEL_INDEX[PALETTE_SIZE] = {
    0,  // Black
    1,  // White
    2,  // Yellow
    3,  // Red
    5,  // Blue  (panel skips index 4)
    6,  // Green
};

int eink_nearest_color(uint8_t r, uint8_t g, uint8_t b) {
    int best = 0;
    int best_dist = 999999;
    for (int i = 0; i < PALETTE_SIZE; i++) {
        int dr = (int)r - PALETTE[i][0];
        int dg = (int)g - PALETTE[i][1];
        int db = (int)b - PALETTE[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

uint8_t eink_palette_to_panel(int palette_idx) {
    if (palette_idx < 0 || palette_idx >= PALETTE_SIZE) return 0;
    return PALETTE_PANEL_INDEX[palette_idx];
}

void eink_dither_rgb888(const uint8_t *src, uint8_t *dst, int w, int h) {
    // Work buffer for error diffusion (allocate in PSRAM)
    uint8_t *work = heap_caps_malloc(w * h * 3, MALLOC_CAP_SPIRAM);
    if (!work) {
        work = malloc(w * h * 3);
    }
    if (!work) return;
    memcpy(work, src, w * h * 3);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 3;
            uint8_t r = work[idx + 0];
            uint8_t g = work[idx + 1];
            uint8_t b = work[idx + 2];

            // Find nearest palette color
            int ci = eink_nearest_color(r, g, b);
            uint8_t rr = PALETTE[ci][0];
            uint8_t gg = PALETTE[ci][1];
            uint8_t bb = PALETTE[ci][2];

            // Write quantized result
            dst[idx + 0] = rr;
            dst[idx + 1] = gg;
            dst[idx + 2] = bb;

            // Quantization error
            int err_r = (int)r - rr;
            int err_g = (int)g - gg;
            int err_b = (int)b - bb;

            // Floyd-Steinberg diffusion:
            //     *   7/16
            // 3/16  5/16  1/16
            if (x + 1 < w) {
                int n = idx + 3;
                work[n + 0] = CLAMP(work[n + 0] + (err_r * 7) / 16, 0, 255);
                work[n + 1] = CLAMP(work[n + 1] + (err_g * 7) / 16, 0, 255);
                work[n + 2] = CLAMP(work[n + 2] + (err_b * 7) / 16, 0, 255);
            }
            if (y + 1 < h) {
                if (x > 0) {
                    int n = ((y + 1) * w + (x - 1)) * 3;
                    work[n + 0] = CLAMP(work[n + 0] + (err_r * 3) / 16, 0, 255);
                    work[n + 1] = CLAMP(work[n + 1] + (err_g * 3) / 16, 0, 255);
                    work[n + 2] = CLAMP(work[n + 2] + (err_b * 3) / 16, 0, 255);
                }
                {
                    int n = ((y + 1) * w + x) * 3;
                    work[n + 0] = CLAMP(work[n + 0] + (err_r * 5) / 16, 0, 255);
                    work[n + 1] = CLAMP(work[n + 1] + (err_g * 5) / 16, 0, 255);
                    work[n + 2] = CLAMP(work[n + 2] + (err_b * 5) / 16, 0, 255);
                }
                if (x + 1 < w) {
                    int n = ((y + 1) * w + (x + 1)) * 3;
                    work[n + 0] = CLAMP(work[n + 0] + (err_r * 1) / 16, 0, 255);
                    work[n + 1] = CLAMP(work[n + 1] + (err_g * 1) / 16, 0, 255);
                    work[n + 2] = CLAMP(work[n + 2] + (err_b * 1) / 16, 0, 255);
                }
            }
        }
    }

    free(work);
}

void eink_scale_bilinear(const uint8_t *src, int src_w, int src_h,
                         uint8_t *dst, int dst_w, int dst_h) {
    const int32_t scale_x = (src_w * 1024) / dst_w;
    const int32_t scale_y = (src_h * 1024) / dst_h;

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int32_t fx = x * scale_x;
            int32_t fy = y * scale_y;

            int x1 = fx / 1024;
            int y1 = fy / 1024;
            int x2 = (x1 + 1 < src_w) ? x1 + 1 : src_w - 1;
            int y2 = (y1 + 1 < src_h) ? y1 + 1 : src_h - 1;

            int wx = fx - x1 * 1024;
            int wy = fy - y1 * 1024;
            int wx1 = 1024 - wx;
            int wy1 = 1024 - wy;

            int off1 = (y1 * src_w + x1) * 3;
            int off2 = (y1 * src_w + x2) * 3;
            int off3 = (y2 * src_w + x1) * 3;
            int off4 = (y2 * src_w + x2) * 3;

            for (int c = 0; c < 3; c++) {
                int v = (src[off1 + c] * wx1 * wy1 + src[off2 + c] * wx * wy1 +
                         src[off3 + c] * wx1 * wy  + src[off4 + c] * wx * wy) / 1048576;
                v = (v < 0) ? 0 : (v > 255) ? 255 : v;
                dst[(y * dst_w + x) * 3 + c] = (uint8_t)v;
            }
        }
    }
}

void eink_rgb565_to_rgb888(const uint8_t *src, uint8_t *dst, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        // RGB565 little-endian: byte0 = GGGBBBBB, byte1 = RRRRRGGG
        uint16_t pixel = src[i * 2] | (src[i * 2 + 1] << 8);
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;
        // Expand to 8 bits: replicate top bits into low bits
        dst[i * 3 + 0] = (r5 << 3) | (r5 >> 2);
        dst[i * 3 + 1] = (g6 << 2) | (g6 >> 4);
        dst[i * 3 + 2] = (b5 << 3) | (b5 >> 2);
    }
}
