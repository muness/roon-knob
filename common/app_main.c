#include "app.h"

#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "bridge_client.h"
#include "controller_config.h"
#include "controller_action_router.h"
#include "controller_input.h"

#include <stdbool.h>

static bool s_controller_initialized;

void app_controller_init(void) {
    if (s_controller_initialized) {
        return;
    }
    controller_action_router_init();
    controller_input_reset_control_mailbox_stats();
    controller_input_set_action_handler(controller_action_router_handle);
    s_controller_initialized = true;
}

void app_entry(void) {
    if (!controller_config_init()) {
        LOGE("configuration owner initialization failed");
        return;
    }

    // Note: mDNS init moved to after WiFi connects (in main_idf.c)
    app_controller_init();
    bridge_client_start();
}
