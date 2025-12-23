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

    // Always boot into Roon mode (Bluetooth mode is alpha and accessed via Settings)
    // If saved zone was Bluetooth, clear it so user selects a Roon zone
    if (controller_mode_is_bluetooth_zone(cfg.zone_id)) {
        cfg.zone_id[0] = '\0';
    }

    // Note: mDNS init moved to after WiFi connects (in main_idf.c)
    ui_set_input_handler(roon_client_handle_input);
    ui_set_zone_name(cfg.zone_id[0] ? cfg.zone_id : "Tap here to select zone");
    roon_client_start(&cfg);
}
