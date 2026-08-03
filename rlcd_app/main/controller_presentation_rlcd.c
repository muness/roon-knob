#include "controller_presentation.h"
#include "rlcd_ui.h"

#include <string.h>

void controller_presentation_update(const char *line1, const char *line2,
                                    const char *line3, bool playing,
                                    float volume, float volume_min,
                                    float volume_max, float volume_step,
                                    int seek_position, int length) {
    rlcd_ui_update(line1, line2, line3, playing, volume, volume_min,
                   volume_max, volume_step, seek_position, length);
}
void controller_presentation_set_status(bool online) { rlcd_ui_set_status(online); }
void controller_presentation_set_message(const char *message) { rlcd_ui_set_message(message); }
void controller_presentation_set_zone_name(const char *zone) { rlcd_ui_set_zone_name(zone); }
void controller_presentation_set_network_status(const char *status) { rlcd_ui_set_network_status(status); }
void controller_presentation_set_artwork(const char *key) { rlcd_ui_set_artwork(key); }
void controller_presentation_show_volume_change(float volume, float step) { rlcd_ui_show_volume_change(volume, step); }
void controller_presentation_update_battery(void) { rlcd_ui_update_battery(); }
void controller_presentation_show_zone_picker(const char **names, const char **ids, int count, int selected) {
    const char *zone_names[18];
    const char *zone_ids[18];
    int zone_count = 0;
    int zone_selected = 0;
    for (int i = 0; i < count && zone_count < 18; ++i) {
        if (!ids || !ids[i] || strcmp(ids[i], "__back__") == 0 ||
            strcmp(ids[i], "__settings__") == 0) {
            continue;
        }
        zone_names[zone_count] = names && names[i] ? names[i] : "Unnamed zone";
        zone_ids[zone_count] = ids[i];
        if (i == selected) zone_selected = zone_count;
        ++zone_count;
    }
    rlcd_ui_show_zone_picker(zone_names, zone_ids, zone_count, zone_selected);
}
void controller_presentation_hide_zone_picker(void) { rlcd_ui_hide_zone_picker(); }
bool controller_presentation_is_zone_picker_visible(void) { return rlcd_ui_is_zone_picker_visible(); }
void controller_presentation_zone_picker_scroll(int delta) { rlcd_ui_zone_picker_scroll(delta); }
bool controller_presentation_zone_picker_is_current_selection(void) { return rlcd_ui_zone_picker_is_current_selection(); }
void controller_presentation_zone_picker_get_selected_id(char *out, size_t length) {
    rlcd_ui_zone_picker_get_selected_id(out, length);
}
void controller_presentation_show_settings(void) {
    rlcd_ui_set_message("Settings: open device web UI");
}
