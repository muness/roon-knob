#include "app.h"

#include "controller_mode.h"
#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "platform/platform_storage.h"
#include "rk_cfg.h"
#include "roon_client.h"
#include "ui.h"

#include <stdbool.h>

void app_entry(void) {
    rk_cfg_t cfg = {0};
    bool valid = platform_storage_load(&cfg) && rk_cfg_is_valid(&cfg);
    if (!valid) {
        LOGI("config missing - applying defaults");
        platform_storage_defaults(&cfg);
        platform_storage_save(&cfg);
    }

    // Check if we should start in Bluetooth mode (saved zone_id is Bluetooth)
    if (controller_mode_is_bluetooth_zone(cfg.zone_id) && controller_mode_bluetooth_available()) {
        LOGI("Saved zone is Bluetooth, starting in BLE mode");
        controller_mode_set(CONTROLLER_MODE_BLUETOOTH);
        // Don't start Roon client in BLE mode, but still pass config for future use
        ui_set_zone_name("Bluetooth");
        roon_client_start(&cfg);
        return;
    }

    // Note: mDNS init moved to after WiFi connects (in main_idf.c)
    ui_set_input_handler(roon_client_handle_input);
    ui_set_zone_name(cfg.zone_id[0] ? cfg.zone_id : "Press knob to select zone");
    roon_client_start(&cfg);
}
