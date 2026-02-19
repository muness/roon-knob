#include "app.h"

#include "bridge_client.h"
#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "platform/platform_storage.h"
#include "rk_cfg.h"
#include "ui.h"
#if USE_MANIFEST
#include "manifest_ui.h"
#endif

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
#if USE_MANIFEST
  manifest_ui_set_input_handler(bridge_client_handle_input);
  manifest_ui_set_zone_name(cfg.zone_id[0] ? cfg.zone_id
                                           : "Tap here to select zone");
#else
  ui_set_input_handler(bridge_client_handle_input);
  ui_set_zone_name(cfg.zone_id[0] ? cfg.zone_id : "Tap here to select zone");
#endif
  bridge_client_start(&cfg);
}
