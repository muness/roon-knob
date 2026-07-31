#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Presentation seam consumed by shared controller code (app_main,
// bridge_client). Exactly one target implementation is link-time selected,
// matching the existing platform_* adapter pattern; shared code carries no
// renderer conditionals and no LVGL/target types.

void controller_presentation_update(const char *line1, const char *line2, const char *line3,
                                     bool playing, float volume, float volume_min,
                                     float volume_max, float volume_step,
                                     int seek_position, int length);
void controller_presentation_set_status(bool online);
void controller_presentation_set_message(const char *msg);
void controller_presentation_set_zone_name(const char *zone_name);
void controller_presentation_set_network_status(const char *status);  // NULL clears the banner
void controller_presentation_set_artwork(const char *image_key);
void controller_presentation_show_volume_change(float volume, float volume_step);
void controller_presentation_update_battery(void);

void controller_presentation_show_zone_picker(const char **zone_names, const char **zone_ids,
                                               int zone_count, int selected_idx);
void controller_presentation_hide_zone_picker(void);
bool controller_presentation_is_zone_picker_visible(void);
void controller_presentation_zone_picker_scroll(int delta);
bool controller_presentation_zone_picker_is_current_selection(void);
void controller_presentation_zone_picker_get_selected_id(char *out, size_t len);

void controller_presentation_show_settings(void);

#ifdef __cplusplus
}
#endif
