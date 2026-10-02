// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//
// Shared HiPhi brand treatment for every device-served web page (captive
// portals, settings servers, power-debug evidence). One stylesheet, one
// header, one footer, so the Dial, Frame, Tough, and any future target look
// like the same product family.
//
// The three fragments are runtime string constants rather than macros, so
// pages always embed them through a "%s" slot instead of concatenating them
// into a format string. That keeps the CSS free to use percentages.

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Product name shown in the header and footer, e.g. "Dial". Targets set this
 * with target_compile_definitions(). */
#ifndef PORTAL_BRAND_PRODUCT
#define PORTAL_BRAND_PRODUCT "Controller"
#endif

#define PORTAL_BRAND_SITE_URL "https://hiphi.audio"

/* Brand stylesheet: dark by default, light under prefers-color-scheme:light.
 * Tokens: hiphi.audio styles.css / unified-hifi-control v4 theme. */
extern const char PORTAL_BRAND_CSS[];

/* Header fragment. One slot: the product name. */
extern const char PORTAL_BRAND_HEADER_FMT[];

/* Footer fragment. Slots: product name, firmware version. */
extern const char PORTAL_BRAND_FOOTER_FMT[];

/* Fragments already rendered for this build. Safe to embed directly; built
 * once on first use and valid for the life of the process. */
const char *portal_brand_header_html(void);
const char *portal_brand_footer_html(void);

/* Running firmware version ("unknown" when the platform cannot report one). */
const char *portal_brand_firmware_version(void);

#ifdef __cplusplus
}
#endif
