// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"
#include "platform/platform_log.h"
#include "platform/platform_touch_navigation.h"

/* Dial and HALO: the bridge's `brightness` (5..100 percent) is the same value their touch slider
 * stores, through the same platform_touch_* path. HALO's wrappers forward to
 * platform_display_{load,save,set}_brightness. Nothing else in the payload applies. */
bool platform_device_settings_apply(const controller_device_settings_t *settings) {
    if (!(settings->present & CDS_BRIGHTNESS)) return true;
    int percent = settings->brightness;
    /* Out of range never reaches here (the parser drops it); it is not a retryable failure. */
    if (percent < CONTROLLER_DEVICE_BRIGHTNESS_MIN || percent > CONTROLLER_DEVICE_BRIGHTNESS_MAX)
        return true;
    if (platform_touch_load_brightness() == percent) return true; /* no NVS write when unchanged */
    if (!platform_touch_save_brightness(percent)) {
        LOGW("device settings: brightness %d was not saved", percent);
        return false;
    }
    platform_touch_set_brightness(percent);
    return true;
}
