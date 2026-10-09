#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void rlcd_ui_init(void);
void rlcd_ui_process(void);
void rlcd_ui_set_status(bool online);
void rlcd_ui_set_message(const char *message);
void rlcd_ui_set_zone_name(const char *zone_name);
void rlcd_ui_set_network_status(const char *status);
void rlcd_ui_set_setup_mode(bool enabled);
void rlcd_ui_set_artwork(const char *image_key);
void rlcd_ui_set_art_mode_timeout(uint32_t timeout_seconds);
bool rlcd_ui_handle_activity(void);
void rlcd_ui_set_key_track_mode(bool track_mode);
bool rlcd_ui_enter_art_mode(void);
/* Returns true once per boot when a KEY gesture dismisses the usage key. */
bool rlcd_ui_dismiss_usage_key(void);
void rlcd_ui_show_volume_change(float volume, float volume_step);
void rlcd_ui_update(const char *line1, const char *line2, const char *line3,
                    bool playing, float volume, float volume_min,
                    float volume_max, float volume_step, int seek_position,
                    int length);
void rlcd_ui_set_ble_status(bool connected);
void rlcd_ui_update_battery(void);
void rlcd_ui_show_zone_picker(const char **names, const char **ids,
                              int count, int selected);
void rlcd_ui_hide_zone_picker(void);
bool rlcd_ui_is_zone_picker_visible(void);
void rlcd_ui_zone_picker_scroll(int delta);
bool rlcd_ui_zone_picker_is_current_selection(void);
void rlcd_ui_zone_picker_get_selected_id(char *out, size_t length);
/** True only while a visible render or setup interaction must remain awake. */
bool rlcd_ui_power_work_pending(void);
