// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"
#include "platform/platform_log.h"
#include "touch_ui.h"

/* StackChan (HIPHI_M5_TARGET_ID 4) only: body language, sounds and sound volume, applied through
 * the setter the portal's POST /api/settings uses. The setter takes all three, so a field the
 * bridge did not send keeps its current value. Dial Lab, Twist and Remote have none of these
 * settings and ignore the payload. */
bool platform_device_settings_apply(const controller_device_settings_t *settings) {
#if HIPHI_M5_TARGET_ID == 4
    const uint32_t mask = CDS_BODY_ENABLED | CDS_SOUND_ENABLED | CDS_SOUND_VOLUME;
    if (!(settings->present & mask)) return true;
    const bool body_now = touch_ui_stackchan_body_preference();
    const bool sound_now = touch_ui_stackchan_sound_preference();
    const uint8_t volume_now = touch_ui_stackchan_voice_volume_preference();
    const bool body = (settings->present & CDS_BODY_ENABLED) ? settings->body_enabled : body_now;
    const bool sound = (settings->present & CDS_SOUND_ENABLED) ? settings->sound_enabled : sound_now;
    uint8_t volume = (settings->present & CDS_SOUND_VOLUME) ? settings->sound_volume : volume_now;
    if (volume > CONTROLLER_DEVICE_SOUND_VOLUME_MAX) return true; /* never reaches here; not retryable */
    if (!touch_ui_apply_stackchan_preferences(body, sound, volume)) {
        LOGW("device settings: StackChan preferences were not applied");
        return false;
    }
    return true;
#else
    (void)settings;
    return true;
#endif
}
