#include "platform/platform_identity.h"

#ifndef HIPHI_DEVICE_SLUG
#error "HIPHI_DEVICE_SLUG must be defined by the target profile"
#endif
#ifndef HIPHI_PROVISIONING_SSID
#error "HIPHI_PROVISIONING_SSID must be defined by the target profile"
#endif

const char *platform_device_slug(void) { return HIPHI_DEVICE_SLUG; }
const char *platform_provisioning_ssid(void) { return HIPHI_PROVISIONING_SSID; }
