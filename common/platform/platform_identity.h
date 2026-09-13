#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Stable lowercase product slug used for default hostnames. */
const char *platform_device_slug(void);

/** SSID exposed while this target is in provisioning mode. */
const char *platform_provisioning_ssid(void);

#ifdef __cplusplus
}
#endif
