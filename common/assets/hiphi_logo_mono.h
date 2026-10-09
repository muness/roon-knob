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

#define HIPHI_LOGO_MONO_SIZE 64
#define HIPHI_LOGO_MONO_STRIDE 8

/*
 * Floyd-Steinberg dithered onto white, packed 1 bit per pixel, MSB first,
 * 8 bytes per row. A set bit means ink (draw the foreground colour);
 * a clear bit means paper (leave the background alone).
 *
 * Used by the Frame's 6-colour ACeP e-ink panel, drawn as EINK_BLACK on the
 * existing EINK_WHITE background inside a refresh the caller already performs.
 */
extern const uint8_t hiphi_logo_mono_64[64 * 8];

/** True when pixel (x, y) is ink. Out-of-range coordinates return false. */
static inline int hiphi_logo_mono_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= 64 || y >= 64) return 0;
    return (hiphi_logo_mono_64[y * 8 + (x >> 3)] >> (7 - (x & 7))) & 1;
}

#ifdef __cplusplus
}
#endif
