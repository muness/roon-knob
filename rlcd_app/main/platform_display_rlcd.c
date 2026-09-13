#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "rlcd_ui.h"
#include "rlcd_power_manager.h"
#include "rlcd_battery.h"

bool platform_display_is_sleeping(void) { return false; }
void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }
void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging) {
    rlcd_ui_set_art_mode_timeout(rk_cfg_get_art_mode_timeout(cfg, is_charging));
}
void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) return;
    out->battery_level = rlcd_battery_percent();
    out->source = PLATFORM_POWER_SOURCE_UNKNOWN;
    out->external_power = false;
}
void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out) {
    rlcd_power_manager_debug_enrich(out);
}
bool platform_power_debug_arm_sleep(uint32_t delay_sec) {
    return rlcd_power_manager_debug_arm(delay_sec);
}
