#pragma once

#include "controller_config.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void touch_ui_init(void);
void touch_ui_process(void);
void touch_ui_set_status(bool online);
void touch_ui_set_message(const char *msg);
void touch_ui_set_zone_name(const char *name);
void touch_ui_set_network_status(const char *status);
void touch_ui_post_zone_name(const char *name);
void touch_ui_post_network_status(const char *status);
void touch_ui_set_artwork(const char *key);
void touch_ui_post_artwork(const char *key);
void touch_ui_show_volume_change(float volume, float step);
void touch_ui_update(const char *line1, const char *line2, const char *line3,
                     bool playing, float volume, float volume_min,
                     float volume_max, float volume_step, int seek_position,
                     int seek_length);
void touch_ui_show_zone_picker(const char **names, const char **ids,
                               int count, int selected);
void touch_ui_hide_zone_picker(void);
bool touch_ui_is_zone_picker_visible(void);
void touch_ui_zone_picker_scroll(int delta);
void touch_ui_zone_picker_get_selected_id(char *out, size_t len);
bool touch_ui_zone_picker_is_current_selection(void);
void touch_ui_update_battery(void);
void touch_ui_apply_display_config(const rk_cfg_t *cfg, bool is_charging);
bool touch_ui_is_display_sleeping(void);
void touch_ui_show_settings(void);
bool touch_ui_stackchan_body_preference(void);
bool touch_ui_stackchan_sound_preference(void);
uint8_t touch_ui_stackchan_voice_volume_preference(void);
bool touch_ui_post_stackchan_preferences(bool body_enabled,
                                         bool sound_enabled,
                                         uint8_t voice_volume);

#ifdef __cplusplus
}
#endif
