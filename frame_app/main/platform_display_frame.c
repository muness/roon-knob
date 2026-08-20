// platform_display.h implementation for e-ink frame
// E-ink has no backlight/sleep — it retains image without power.

#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "frame_power_manager.h"
#include "pmic_axp2101.h"

bool platform_display_is_sleeping(void) {
    return false;  // E-ink always "displays" — no sleep concept
}

void platform_display_set_rotation(uint16_t degrees) {
    (void)degrees;  // Fixed orientation on frame — no rotation
}

void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging) {
    (void)cfg;
    (void)is_charging;
    // Frame has no LCD backlight or Dial-style dim/sleep state. E-ink power
    // policy is target-owned and is qualified separately under issue #160.
}

void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) {
        return;
    }
    out->battery_level = pmic_get_battery_percent();
    out->external_power =
        pmic_power_source() == PMIC_POWER_SOURCE_EXTERNAL;
}

void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out) {
    frame_power_manager_debug_enrich(out);
}

bool platform_power_debug_arm_sleep(uint32_t delay_sec) {
    return frame_power_manager_debug_arm(delay_sec);
}
