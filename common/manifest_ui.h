#pragma once

/// Manifest-driven knob UI — screen renderers and navigation.
///
/// Implements the knob UI with manifest-driven screen rendering.

#include "manifest_parse.h"
#include "ui.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Initialize the manifest UI system.
/// Creates the root LVGL container and screen slots.
/// Call once after lv_init() and display driver setup.
void manifest_ui_init(void);

/// Set the input handler callback.
void manifest_ui_set_input_handler(ui_input_cb_t handler);

/// Apply manifest state to the UI.
/// - Always applies fast state (volume, seek, transport).
/// - Re-renders screens only if SHA changed from last call.
/// Call from the UI thread (via platform_task_post_to_ui).
void manifest_ui_update(const manifest_t *manifest);

/// Navigate between screens.
/// delta: -1 = previous, +1 = next (wraps around).
void manifest_ui_navigate(int delta);

/// Get the current screen type (for input routing).
screen_type_t manifest_ui_current_screen_type(void);

/// Get the current screen ID.
const char *manifest_ui_current_screen_id(void);

/// Show the zone picker overlay (triggered by header tap).
/// Uses the zones from the current manifest's list screen.
void manifest_ui_show_zone_picker(void);

/// Hide the zone picker overlay.
void manifest_ui_hide_zone_picker(void);

/// Check if zone picker is visible.
bool manifest_ui_is_zone_picker_visible(void);

/// Scroll zone picker selection.
void manifest_ui_zone_picker_scroll(int delta);

/// Get selected zone ID from picker.
void manifest_ui_zone_picker_get_selected_id(char *out, size_t len);

/// Check if selected zone matches the current zone.
bool manifest_ui_zone_picker_is_current_selection(void);

/// Set the zone name in the header.
void manifest_ui_set_zone_name(const char *name);

/// Set online/offline status dot.
void manifest_ui_set_status(bool online);

/// Show a transient status message.
void manifest_ui_set_message(const char *msg);

/// Set album artwork by image key (triggers async fetch).
bool manifest_ui_set_artwork(const char *image_key);

/// Show volume change overlay (optimistic UI during rotary input).
void manifest_ui_show_volume_change(float vol, float vol_step);

/// Set network status banner (persistent, NULL to clear).
void manifest_ui_set_network_status(const char *status);

#ifdef __cplusplus
}
#endif
