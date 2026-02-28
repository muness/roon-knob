#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_INPUT_VOL_DOWN = -1,
    UI_INPUT_NONE = 0,
    UI_INPUT_VOL_UP = 1,
    UI_INPUT_PLAY_PAUSE = 2,
    UI_INPUT_MENU = 3,
    UI_INPUT_NEXT_TRACK = 4,
    UI_INPUT_PREV_TRACK = 5,
    UI_INPUT_MUTE = 6,
} ui_input_event_t;

typedef void (*ui_input_cb_t)(ui_input_event_t event);

void ui_loop_iter(void);
void ui_set_message(const char *msg);
void ui_set_zone_name(const char *zone_name);

// Settings UI (platform-specific implementation)
void ui_show_settings(void);  // Show settings panel (long-press zone label)
void ui_hide_settings(void);  // Hide settings panel
bool ui_is_settings_visible(void);  // Check if settings panel is visible

// OTA update UI
void ui_set_update_available(const char *version);  // Show update notification (NULL to hide)
void ui_set_update_progress(int percent);  // Show update progress (-1 to hide)
void ui_trigger_update(void);  // Called when user taps update notification

// Display state control
void ui_set_controls_visible(bool visible);  // Show/hide UI controls for art mode

// Network status banner (persistent, doesn't auto-clear)
void ui_set_network_status(const char *status);  // Show persistent network status (NULL to clear)

// Battery indicator
void ui_update_battery(void);  // Force battery display refresh (call on USB connect/disconnect)

#ifdef __cplusplus
}
#endif
