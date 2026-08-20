#include "platform/platform_display.h"
#include "platform/platform_power.h"
#include "touch_ui.h"

bool platform_display_is_sleeping(void) { return touch_ui_is_display_sleeping(); }
void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }
void platform_display_apply_config(const rk_cfg_t *cfg, bool charging) {
    touch_ui_apply_display_config(cfg, charging);
}
void platform_power_snapshot(platform_power_snapshot_t *out) {
    if (!out) return;
    out->battery_level = -1;
    out->external_power = false;
}
