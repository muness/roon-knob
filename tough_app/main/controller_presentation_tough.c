// Tough implementation of the target-neutral controller presentation seam.
// Thin adapter over touch_ui.c's LVGL-based renderer, mirroring
// frame_app/main/controller_presentation_frame.c's structure exactly
// (including the config_durability_warning() pattern, which is shared
// boilerplate copied from Frame/Dial, not Frame-specific).

#include "controller_presentation.h"
#include "controller_config.h"
#include "touch_ui.h"

static const char *config_durability_warning(void) {
    controller_config_snapshot_t config;
    if (!controller_config_snapshot(&config)) {
        return NULL;
    }
    if (config.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT) {
        return "Settings saved but could\nnot be verified";
    }
    if (config.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY) {
        return "Settings storage unavailable;\nchanges may not survive";
    }
    return NULL;
}

void controller_presentation_update(const char *line1, const char *line2,
                                    const char *line3, bool playing,
                                    float volume, float volume_min,
                                    float volume_max, float volume_step,
                                    int seek_position, int length) {
    touch_ui_update(line1, line2, line3, playing, volume, volume_min,
                    volume_max, volume_step, seek_position, length);
}

void controller_presentation_set_status(bool online) {
    touch_ui_set_status(online);
}

void controller_presentation_set_message(const char *msg) {
    touch_ui_set_message(msg);
}

void controller_presentation_set_zone_name(const char *zone_name) {
    touch_ui_set_zone_name(zone_name);
}

void controller_presentation_set_network_status(const char *status) {
    const char *warning = config_durability_warning();
    touch_ui_set_network_status(warning ? warning : status);
}

void controller_presentation_set_artwork(const char *image_key) {
    touch_ui_set_artwork(image_key);
}

void controller_presentation_show_volume_change(float volume, float volume_step) {
    touch_ui_show_volume_change(volume, volume_step);
}

void controller_presentation_update_battery(void) {
    touch_ui_update_battery();
}

void controller_presentation_show_zone_picker(const char **zone_names,
                                              const char **zone_ids,
                                              int zone_count,
                                              int selected_idx) {
    touch_ui_show_zone_picker(zone_names, zone_ids, zone_count, selected_idx);
}

void controller_presentation_hide_zone_picker(void) {
    touch_ui_hide_zone_picker();
}

bool controller_presentation_is_zone_picker_visible(void) {
    return touch_ui_is_zone_picker_visible();
}

void controller_presentation_zone_picker_scroll(int delta) {
    touch_ui_zone_picker_scroll(delta);
}

bool controller_presentation_zone_picker_is_current_selection(void) {
    return touch_ui_zone_picker_is_current_selection();
}

void controller_presentation_zone_picker_get_selected_id(char *out, size_t len) {
    touch_ui_zone_picker_get_selected_id(out, len);
}

void controller_presentation_show_settings(void) {
    touch_ui_show_settings();
}
