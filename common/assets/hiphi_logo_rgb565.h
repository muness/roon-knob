/*
 * GENERATED FILE - DO NOT EDIT BY HAND.
 *
 * Regenerate with:  python3 scripts/generate_boot_logo.py
 * Source master:    docs/images/hiphi-logo-512.png
 *
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HiPhi mark for the M5GFX/LovyanGFX targets, at 64, 48, 32 px. */

#define HIPHI_LOGO_64_STRIDE 8
/* Host-order RGB565: (R>>3)<<11 | (G>>2)<<5 | (B>>3). NOT byte-swapped. */
extern const uint16_t hiphi_logo_rgb565_64[64 * 64];
/* 1 bit per pixel, MSB first, 8 bytes per row. 1 = opaque. */
extern const uint8_t hiphi_logo_alpha_64[64 * 8];
#define HIPHI_LOGO_48_STRIDE 6
/* Host-order RGB565: (R>>3)<<11 | (G>>2)<<5 | (B>>3). NOT byte-swapped. */
extern const uint16_t hiphi_logo_rgb565_48[48 * 48];
/* 1 bit per pixel, MSB first, 6 bytes per row. 1 = opaque. */
extern const uint8_t hiphi_logo_alpha_48[48 * 6];
#define HIPHI_LOGO_32_STRIDE 4
/* Host-order RGB565: (R>>3)<<11 | (G>>2)<<5 | (B>>3). NOT byte-swapped. */
extern const uint16_t hiphi_logo_rgb565_32[32 * 32];
/* 1 bit per pixel, MSB first, 4 bytes per row. 1 = opaque. */
extern const uint8_t hiphi_logo_alpha_32[32 * 4];

#ifdef __cplusplus
}  /* extern "C" */

/*
 * Draw the mark over a solid background colour.
 *
 * Byte order matters here. LovyanGFX picks the source pixel format from
 * `get_depth<T>`: for a plain `uint16_t` that resolves to `rgb565_2Byte`,
 * which is the BYTE-SWAPPED layout, while `lgfx::rgb565_t` is
 * `rgb565_nonswapped` - host order, exactly what this file stores. Passing the
 * arrays as `const uint16_t*` would therefore swap red and blue, so the helper
 * casts to `const lgfx::rgb565_t*`.
 *
 * Templated on the GFX type so this header needs no M5GFX include; instantiate
 * it from a translation unit that already has M5GFX/LovyanGFX in scope.
 *
 * Composites one row at a time into a small stack buffer - no allocation, no
 * sprite, and a single pushImage per row.
 */
template <typename GFX>
inline void hiphi_logo_draw_rgb565(GFX &gfx, int x, int y, int size,
                                   uint32_t background_rgb888) {
    const uint16_t *pixels = nullptr;
    const uint8_t *alpha = nullptr;
    int stride = 0;
    switch (size) {
    case 64:
        pixels = hiphi_logo_rgb565_64;
        alpha = hiphi_logo_alpha_64;
        stride = HIPHI_LOGO_64_STRIDE;
        break;
    case 48:
        pixels = hiphi_logo_rgb565_48;
        alpha = hiphi_logo_alpha_48;
        stride = HIPHI_LOGO_48_STRIDE;
        break;
    case 32:
        pixels = hiphi_logo_rgb565_32;
        alpha = hiphi_logo_alpha_32;
        stride = HIPHI_LOGO_32_STRIDE;
        break;
    default:
        return;
    }

    const uint16_t bg = static_cast<uint16_t>(
        (((background_rgb888 >> 16) & 0xff) >> 3) << 11 |
        (((background_rgb888 >> 8) & 0xff) >> 2) << 5 |
        ((background_rgb888 & 0xff) >> 3));

    uint16_t row[64];
    for (int ry = 0; ry < size; ++ry) {
        const uint8_t *mask = alpha + ry * stride;
        const uint16_t *src = pixels + ry * size;
        for (int rx = 0; rx < size; ++rx) {
            const bool opaque = (mask[rx >> 3] >> (7 - (rx & 7))) & 1;
            row[rx] = opaque ? src[rx] : bg;
        }
        gfx.pushImage(x, y + ry, size, 1,
                      reinterpret_cast<const lgfx::rgb565_t *>(row));
    }
}
#endif /* __cplusplus */
