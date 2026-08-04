#include "platform/platform_display.h"
#include "rlcd_ui.h"

bool platform_display_is_sleeping(void) { return false; }
void platform_display_set_rotation(uint16_t degrees) { (void)degrees; }
void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging) {
    rlcd_ui_set_art_mode_timeout(rk_cfg_get_art_mode_timeout(cfg, is_charging));
}
bool platform_battery_is_charging(void) { return false; }
int platform_battery_get_level(void) { return -1; }
