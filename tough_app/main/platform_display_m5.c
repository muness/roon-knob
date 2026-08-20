/* Compatibility implementation for shared controller code.
 * Rendering and input are owned by the M5Unified/M5GFX platform component;
 * these hooks retain the common display/power contract without reintroducing
 * a second panel driver or LVGL dependency. */

#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "touch_ui.h"

bool platform_display_is_sleeping(void) {
    return touch_ui_is_display_sleeping();
}

void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }

void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging) {
    touch_ui_apply_display_config(cfg, is_charging);
}

/* Tough is a USB-powered appliance with no user-replaceable battery. Treat it
 * as externally powered so the shared defaults keep sleep disabled while
 * still allowing album-art and dim stages. */
void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) return;
    out->battery_level = -1;
    out->external_power = true;
}
