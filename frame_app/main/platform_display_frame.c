// platform_display.h implementation for e-ink frame
// E-ink has no backlight/sleep — it retains image without power.

#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "frame_power_manager.h"
#include "pmic_axp2101.h"
#include <esp_timer.h>

#define FRAME_POWER_SNAPSHOT_CACHE_US 15000000LL
static platform_power_snapshot_t s_cached_power = {
    .battery_level = -1,
    .source = PLATFORM_POWER_SOURCE_UNKNOWN,
    .external_power = true,
};
static int64_t s_cached_power_us;

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
    const int64_t now = esp_timer_get_time();
    if (s_cached_power_us != 0 &&
        now - s_cached_power_us < FRAME_POWER_SNAPSHOT_CACHE_US) {
        *out = s_cached_power;
        return;
    }
    s_cached_power.battery_level = pmic_get_battery_percent();
    const pmic_power_source_t source = pmic_power_source();
    s_cached_power.source = source == PMIC_POWER_SOURCE_EXTERNAL
        ? PLATFORM_POWER_SOURCE_EXTERNAL
        : source == PMIC_POWER_SOURCE_BATTERY
            ? PLATFORM_POWER_SOURCE_BATTERY
            : PLATFORM_POWER_SOURCE_UNKNOWN;
    s_cached_power.external_power = source != PMIC_POWER_SOURCE_BATTERY;
    s_cached_power_us = now;
    *out = s_cached_power;
}

void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out) {
    frame_power_manager_debug_enrich(out);
}

bool platform_power_debug_arm_sleep(uint32_t delay_sec) {
    return frame_power_manager_debug_arm(delay_sec);
}
