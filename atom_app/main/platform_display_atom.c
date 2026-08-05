#include "platform/platform_display.h"
#include "touch_ui.h"

bool platform_display_is_sleeping(void) { return touch_ui_is_display_sleeping(); }
void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }
void platform_display_apply_config(const rk_cfg_t *cfg, bool charging) {
    touch_ui_apply_display_config(cfg, charging);
}
bool platform_battery_is_charging(void) { return false; }
int platform_battery_get_level(void) { return -1; }
