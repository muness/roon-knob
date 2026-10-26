// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"
#include "eink_ui.h"
#include "frame_display_preferences.h"
#include "platform/platform_log.h"

/* Frame: `show_ip`, applied exactly as the portal's display form does (captive_portal.c
 * sta_display_set_handler): persist, then post the change to the UI task. */
void platform_device_settings_apply(const controller_device_settings_t *settings) {
    if (!(settings->present & CDS_SHOW_IP)) return;
    if (frame_display_preferences_show_ip() == settings->show_ip) return; /* idempotent */
    if (!frame_display_preferences_set_show_ip(settings->show_ip)) {
        LOGW("device settings: show_ip was not saved");
        return;
    }
    eink_ui_post_show_ip(settings->show_ip);
}
