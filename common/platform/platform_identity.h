#pragma once

/** Stable lowercase product slug used for default hostnames. */
const char *platform_device_slug(void);

/** SSID exposed while this target is in provisioning mode. */
const char *platform_provisioning_ssid(void);
