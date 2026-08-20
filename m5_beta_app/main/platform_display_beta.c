#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "m5_platform.h"
#include "touch_ui.h"

bool platform_display_is_sleeping(void) {
    return touch_ui_is_display_sleeping();
}

void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }

void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging) {
    touch_ui_apply_display_config(cfg, is_charging);
}

void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) {
        return;
    }
    out->battery_level = m5_platform_battery_level();
    out->external_power = m5_platform_battery_is_charging();
}
void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out) {
    if (!out) return;
    out->capabilities = PLATFORM_POWER_CAP_DISPLAY_SLEEP |
        PLATFORM_POWER_CAP_BOARD_POWER_OFF;
}
