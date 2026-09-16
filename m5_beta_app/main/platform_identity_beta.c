#include "platform/platform_identity.h"

#ifndef HIPHI_DEVICE_SLUG
#error "HIPHI_DEVICE_SLUG must be defined by the target profile"
#endif
#ifndef HIPHI_PROVISIONING_SSID
#error "HIPHI_PROVISIONING_SSID must be defined by the target profile"
#endif

const char *platform_device_slug(void) { return HIPHI_DEVICE_SLUG; }
const char *platform_provisioning_ssid(void) { return HIPHI_PROVISIONING_SSID; }

/* Canonical product names for the M5 beta family (issue #164). Kizz is
 * deliberately not prefixed with "HiPhi" - it is its own character. */
#if HIPHI_M5_TARGET_ID == 1
#define HIPHI_PRODUCT_DISPLAY_NAME "HiPhi Dial Lab"
#elif HIPHI_M5_TARGET_ID == 2
#define HIPHI_PRODUCT_DISPLAY_NAME "HiPhi Twist"
#elif HIPHI_M5_TARGET_ID == 3
#define HIPHI_PRODUCT_DISPLAY_NAME "HiPhi Remote"
#elif HIPHI_M5_TARGET_ID == 4
#define HIPHI_PRODUCT_DISPLAY_NAME "Kizz"
#else
#define HIPHI_PRODUCT_DISPLAY_NAME "HiPhi"
#endif

const char *platform_product_name(void) { return HIPHI_PRODUCT_DISPLAY_NAME; }
