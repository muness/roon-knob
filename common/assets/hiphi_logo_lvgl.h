/*
 * GENERATED FILE - DO NOT EDIT BY HAND.
 *
 * Regenerate with:  python3 scripts/generate_boot_logo.py
 * Source master:    docs/images/hiphi-logo-512.png
 *
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HIPHI_LOGO_LVGL_SIZE 64

/*
 * Native sizes rather than one asset plus lv_image_set_scale(): a scaled
 * lv_image keeps its source-sized bounding box, so the Dial's smaller mark
 * would be clipped. The Dial uses the 48 px descriptor (see common/ui.c for
 * why 48 and not 64); Slate uses the 64 px A8 mask below.
 */
/* Full-colour 64 px mark, ARGB8888. */
extern const lv_image_dsc_t hiphi_logo_64_argb8888;

/* Full-colour 48 px mark, ARGB8888. */
extern const lv_image_dsc_t hiphi_logo_48_argb8888;

/* 64 px 1-bit ink mask as an alpha-only image. LVGL recolors it with the
 * image's recolor style property, so set that to black. Used by Slate
 * (rlcd_app), whose panel is 1-bit: pure black/white survives the panel's own
 * Bayer threshold exactly instead of being re-dithered. */
extern const lv_image_dsc_t hiphi_logo_mono_64_a8;

#ifdef __cplusplus
}
#endif
