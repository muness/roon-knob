// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"
#include "eink_ui.h"
#include "frame_display_preferences.h"
#include "platform/platform_log.h"

/* Frame: remote and portal show_ip requests are admitted by the settings owner
 * on the UI actor. Return success only after persistence and effective application. */
bool platform_device_settings_apply(const controller_device_settings_t *settings) {
    if (!(settings->present & CDS_SHOW_IP)) return true;
    if (frame_display_preferences_show_ip() != settings->show_ip &&
        !frame_display_preferences_set_show_ip(settings->show_ip)) {
        LOGW("device settings: show_ip was not saved");
        return false;
    }
    eink_ui_apply_show_ip(settings->show_ip);
    return true;
}
