/* Headless implementation of the Tough touch_ui API, so the shared Tough captive portal and the
 * HALO boot loop compile unchanged. Network/zone status goes to the log. */
#include "touch_ui.h"
#include <esp_log.h>

static const char *TAG = "halo_ui";

void touch_ui_init(void) {}
void touch_ui_process(void) {}
void touch_ui_post_network_status(const char *status) { if (status) ESP_LOGI(TAG, "net: %s", status); }
void touch_ui_post_zone_name(const char *name) { if (name) ESP_LOGI(TAG, "zone: %s", name); }
bool touch_ui_is_display_sleeping(void) { return false; }
bool touch_ui_stackchan_body_preference(void) { return false; }
bool touch_ui_stackchan_sound_preference(void) { return false; }
uint8_t touch_ui_stackchan_voice_volume_preference(void) { return 0; }
bool touch_ui_post_stackchan_preferences(bool body_enabled, bool sound_enabled, uint8_t voice_volume) {
    (void)body_enabled; (void)sound_enabled; (void)voice_volume;
    return false;
}
