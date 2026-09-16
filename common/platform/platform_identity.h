#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Stable lowercase product slug used for default hostnames. */
const char *platform_device_slug(void);

/** SSID exposed while this target is in provisioning mode. */
const char *platform_provisioning_ssid(void);

/**
 * Human-facing product name, e.g. "HiPhi Dial". Used as the title on boot and
 * Wi-Fi setup screens. Canonical names live in the product table in issue #164.
 */
const char *platform_product_name(void);

#ifdef __cplusplus
}
#endif
