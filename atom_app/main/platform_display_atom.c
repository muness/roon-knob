#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "m5_platform.h"
#include "m5_terminal_power.h"
#include "touch_ui.h"

bool platform_display_is_sleeping(void) { return touch_ui_is_display_sleeping(); }
void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }
void platform_display_apply_config(const rk_cfg_t *cfg, bool charging) {
    touch_ui_apply_display_config(cfg, charging);
}
void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) return;
    m5_platform_power_snapshot_t snapshot = {0};
    if (!m5_platform_power_snapshot(&snapshot)) return;
    out->battery_level = snapshot.battery_level;
    out->source = snapshot.source == M5_PLATFORM_POWER_SOURCE_EXTERNAL
        ? PLATFORM_POWER_SOURCE_EXTERNAL
        : snapshot.source == M5_PLATFORM_POWER_SOURCE_BATTERY
            ? PLATFORM_POWER_SOURCE_BATTERY : PLATFORM_POWER_SOURCE_UNKNOWN;
    out->external_power = snapshot.external_power_policy;
}
void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out) {
    m5_terminal_power_diagnostics(out);
    if (out) out->capabilities |= PLATFORM_POWER_CAP_AUXILIARY_SOC;
}
bool platform_power_debug_arm_sleep(uint32_t delay_sec) {
    return m5_terminal_power_debug_arm(delay_sec);
}
