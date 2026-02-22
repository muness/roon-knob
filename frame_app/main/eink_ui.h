#pragma once

#include <stdbool.h>
#include <stddef.h>

// E-ink UI — replaces LVGL ui.c for the frame display.
// Called from bridge_client.c via USE_EINK dispatch macros.

void eink_ui_init(void);

// Called when bridge status changes (online/offline)
void eink_ui_set_status(bool online);

// Show a message (e.g., "Bridge: Connected", "WiFi: Connecting...")
void eink_ui_set_message(const char *msg);

// Update zone name in header
void eink_ui_set_zone_name(const char *name);

// Set network status banner
void eink_ui_set_network_status(const char *status);

// New album artwork available — triggers download + dither + display
void eink_ui_set_artwork(const char *image_key);

// Show volume change overlay (just updates volume text on next refresh)
void eink_ui_show_volume_change(float vol, float vol_step);

// Now-playing state update (track info)
void eink_ui_update(const char *line1, const char *line2, bool playing,
                    float volume, float volume_min, float volume_max,
                    float volume_step, int seek_position, int length);

// Zone picker (simplified for e-ink — just cycle zones with button)
void eink_ui_show_zone_picker(void);
void eink_ui_hide_zone_picker(void);
bool eink_ui_is_zone_picker_visible(void);
void eink_ui_zone_picker_scroll(int delta);
void eink_ui_zone_picker_get_selected_id(char *out, size_t len);
bool eink_ui_zone_picker_is_current_selection(void);

// Input handler registration (called from app_main.c)
#include "ui.h"
void eink_ui_set_input_handler(ui_input_cb_t handler);

// BLE remote connection status (piggybacks on next now-playing refresh)
void eink_ui_set_ble_status(bool connected);

// Battery display refresh (called from bridge_client.c)
void eink_ui_update_battery(void);

// Settings panel (noop for e-ink)
void eink_ui_show_settings(void);

// Process pending UI updates (call from main loop)
void eink_ui_process(void);
