/* HALO V2 display bus pins are not known yet: no display output. The board is USB-powered with no
 * battery, so power reporting is static external power. */
#include "platform/platform_display.h"
#include "platform/platform_power.h"

bool platform_display_is_sleeping(void) { return false; }
void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }
void platform_display_apply_config(const rk_cfg_t *cfg, bool charging) { (void)cfg; (void)charging; }

void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) return;
    out->battery_level = -1;
    out->source = PLATFORM_POWER_SOURCE_EXTERNAL;
}
void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out) { (void)out; }
bool platform_power_debug_arm_sleep(uint32_t delay_sec) { (void)delay_sec; return false; }
