#include "app.h"

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
    // Note: mDNS init moved to after WiFi connects (in main_idf.c)
    ui_set_input_handler(roon_client_handle_input);
    ui_set_zone_name(cfg.zone_id[0] ? cfg.zone_id : "Press knob to select zone");
    roon_client_start(&cfg);
}
