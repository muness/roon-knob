#pragma once

// LVGL-based touch UI for M5Stack Tough's 320x240 ILI9342C screen. This is
// the target-owned renderer wrapped by controller_presentation_tough.c,
// mirroring the eink_ui.h contract shape used by frame_app/main/eink_ui.h
// (including the target-local now-playing 3-line contract and the
// post_*() deferred-update pair for cross-task safety).
//
// Scope note (Alpha): this reproduces Dial's (common/ui.c) interaction model
// at reduced scope -- now-playing text, play/pause, prev/next, volume via
// +/- buttons (Tough has no rotary encoder, so no volume ring), a
// touch-scrollable zone list, a status/network banner, and a minimal
// settings screen. It intentionally does not attempt Dial's volume-ring
// widget, artwork rendering, or OTA update UI.
//
// TODO(hardware-verify): touch layout and hit-target sizing in touch_ui.c
// have NOT been validated against physical Tough hardware.

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void touch_ui_init(void);

// Call every iteration from the UI task: pumps LVGL's timer handler and
// applies any deferred touch_ui_post_*() updates queued from other tasks.
void touch_ui_process(void);

// Called when bridge status changes (online/offline)
void touch_ui_set_status(bool online);

// Show a transient message (e.g. "Hi-Fi Control: Connected")
void touch_ui_set_message(const char *msg);

// Update zone name in header
void touch_ui_set_zone_name(const char *zone_name);

// Set persistent network status banner (NULL clears it)
void touch_ui_set_network_status(const char *status);

// Queue target-local status changes from WiFi/HTTP callbacks (not the UI
// task) onto the UI loop, matching frame_app/main/eink_ui.h's post_*() pair.
void touch_ui_post_zone_name(const char *name);
void touch_ui_post_network_status(const char *status);

// New album artwork available. Alpha scope: this Tough renderer does not
// decode or display artwork; the call is accepted (and logged) so the
// shared bridge_client code needs no target conditionals.
void touch_ui_set_artwork(const char *image_key);

// Show volume change (updates the volume label on next refresh)
void touch_ui_show_volume_change(float vol, float vol_step);

// Now-playing state update (track info). line3 is genuinely rendered (a
// third label), unlike Dial's common/ui.c which only has two now-playing
// lines and ignores a third -- see controller_presentation_dial.c.
void touch_ui_update(const char *line1, const char *line2, const char *line3,
                     bool playing, float volume, float volume_min,
                     float volume_max, float volume_step, int seek_position,
                     int length);

// Zone picker: touch-scrollable list, tap to select.
void touch_ui_show_zone_picker(const char **zone_names, const char **zone_ids,
                               int zone_count, int selected_idx);
void touch_ui_hide_zone_picker(void);
bool touch_ui_is_zone_picker_visible(void);
void touch_ui_zone_picker_scroll(int delta);
void touch_ui_zone_picker_get_selected_id(char *out, size_t len);
bool touch_ui_zone_picker_is_current_selection(void);

// Battery display refresh. Tough's AXP192 read is not wired up this Alpha
// (see platform_display_tough.c); platform_battery_get_level() returns -1
// and this renders as unavailable.
void touch_ui_update_battery(void);

// Minimal settings screen, reachable via long-press on the zone label
// (mirrors Dial's convention in common/ui.c).
void touch_ui_show_settings(void);

#ifdef __cplusplus
}
#endif
