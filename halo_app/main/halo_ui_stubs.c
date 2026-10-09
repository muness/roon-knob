/* Dial-only UI hooks that HALO does not have yet: on-device settings panel (ui_network.c) and OTA. */
#include "ui.h"
#include "ota_update.h"
#include <esp_log.h>

void ui_show_settings(void) {}
void ui_hide_settings(void) {}
bool ui_is_settings_visible(void) { return false; }
void ota_start_update(void) { ESP_LOGW("ota", "OTA not supported on HALO yet"); }
