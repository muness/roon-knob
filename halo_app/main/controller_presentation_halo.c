/* Headless presentation: state goes to the log (and later the haptic/web page). */
#include "controller_presentation.h"
#include <esp_log.h>

static const char *TAG = "halo_view";

void controller_presentation_update(const char *a, const char *b, const char *c, bool playing, float volume,
                                    float min, float max, float step, int pos, int length) {
    (void)c; (void)min; (void)max; (void)step; (void)pos; (void)length;
    ESP_LOGI(TAG, "%s %s - %s  vol %.1f", playing ? "PLAY" : "PAUSE", a ? a : "", b ? b : "", volume);
}
void controller_presentation_set_status(bool online) { ESP_LOGI(TAG, "bridge %s", online ? "online" : "offline"); }
void controller_presentation_set_message(const char *v) { if (v && *v) ESP_LOGI(TAG, "msg: %s", v); }
void controller_presentation_set_zone_name(const char *v) { ESP_LOGI(TAG, "zone: %s", v ? v : "-"); }
void controller_presentation_set_zone_count(int count) { (void)count; }
void controller_presentation_set_network_status(const char *v) { if (v) ESP_LOGI(TAG, "net: %s", v); }
void controller_presentation_set_artwork(const char *v) { (void)v; }
void controller_presentation_show_volume_change(float v, float s) { (void)s; ESP_LOGI(TAG, "volume %.1f", v); }
void controller_presentation_update_battery(void) {}
void controller_presentation_show_zone_picker(const char **n, const char **i, int c, int s) { (void)n; (void)i; (void)c; (void)s; }
void controller_presentation_hide_zone_picker(void) {}
bool controller_presentation_is_zone_picker_visible(void) { return false; }
void controller_presentation_zone_picker_scroll(int d) { (void)d; }
bool controller_presentation_zone_picker_is_current_selection(void) { return false; }
void controller_presentation_zone_picker_get_selected_id(char *o, size_t l) { if (o && l) o[0] = 0; }
void controller_presentation_show_settings(void) {}
