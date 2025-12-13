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
} ui_input_event_t;

typedef void (*ui_input_cb_t)(ui_input_event_t event);

void ui_init(void);
void ui_loop_iter(void);
void ui_update(const char *line1, const char *line2, bool playing, int volume, int volume_min, int volume_max, int seek_position, int length);
void ui_set_status(bool online);
void ui_set_message(const char *msg);
void ui_set_input_handler(ui_input_cb_t handler);
void ui_dispatch_input(ui_input_event_t ev);
void ui_set_zone_name(const char *zone_name);
void ui_show_zone_picker(const char **zone_names, const char **zone_ids, int zone_count, int selected_idx);
void ui_hide_zone_picker(void);
bool ui_is_zone_picker_visible(void);
int ui_zone_picker_get_selected(void);
void ui_zone_picker_get_selected_id(char *out, size_t len);
void ui_zone_picker_scroll(int delta);
void ui_set_artwork(const char *image_key);  // Set album artwork (placeholder for now)
void ui_show_volume_change(int vol);  // Show volume overlay when adjusting
void ui_test_pattern(void);  // Debug: Show RGB test pattern to verify color format

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

// BLE mode UI
typedef enum {
    UI_BLE_STATE_DISABLED,
    UI_BLE_STATE_ADVERTISING,
    UI_BLE_STATE_CONNECTED
} ui_ble_state_t;

void ui_set_ble_mode(bool enabled);  // Switch UI to BLE mode visuals
void ui_set_ble_status(ui_ble_state_t state, const char *device_name);  // Update BLE connection status

// Network status banner (persistent, doesn't auto-clear)
void ui_set_network_status(const char *status);  // Show persistent network status (NULL to clear)

// Exit Bluetooth confirmation dialog
typedef void (*ui_exit_bt_callback_t)(bool confirmed);  // Called with true if user confirms exit
void ui_show_exit_bt_dialog(ui_exit_bt_callback_t callback);  // Show "Exit Bluetooth?" dialog
void ui_hide_exit_bt_dialog(void);
bool ui_is_exit_bt_dialog_visible(void);

#ifdef __cplusplus
}
#endif
