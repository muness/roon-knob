/* touch_ui shim: the shared Tough captive portal and boot code call the touch_ui_* API. On HALO the real
 * screen is common/ui.c (LVGL), so these forward to it; the StackChan-only hooks are inert. */
#include "touch_ui.h"
#include "ui.h"

void touch_ui_init(void) {}
void touch_ui_process(void) {}
void touch_ui_post_network_status(const char *status) { ui_set_network_status(status); }
void touch_ui_post_zone_name(const char *name) { if (name) ui_set_zone_name(name); }
bool touch_ui_is_display_sleeping(void) { return false; }
bool touch_ui_stackchan_body_preference(void) { return false; }
bool touch_ui_stackchan_sound_preference(void) { return false; }
uint8_t touch_ui_stackchan_voice_volume_preference(void) { return 0; }
bool touch_ui_post_stackchan_preferences(bool body_enabled, bool sound_enabled, uint8_t voice_volume) {
    (void)body_enabled; (void)sound_enabled; (void)voice_volume;
    return false;
}
