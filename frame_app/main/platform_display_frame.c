// platform_display.h implementation for e-ink frame
// E-ink has no backlight/sleep — it retains image without power.

#include "platform/platform_display.h"
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

bool platform_battery_is_charging(void) {
    return pmic_is_charging();
}

int platform_battery_get_level(void) {
    return pmic_get_battery_percent();
}
