#include "platform/platform_display.h"
#include "m5_platform.h"
#include "touch_ui.h"

bool platform_display_is_sleeping(void) {
    return touch_ui_is_display_sleeping();
}

void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }

void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging) {
    touch_ui_apply_display_config(cfg, is_charging);
}

bool platform_battery_is_charging(void) {
    return m5_platform_battery_is_charging();
}

int platform_battery_get_level(void) {
    return m5_platform_battery_level();
}
