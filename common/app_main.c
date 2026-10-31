#include "app.h"
#include "crash_report.h"

#include "platform/platform_log.h"
#include "platform/platform_mdns.h"
#include "bridge_client.h"
#include "controller_config.h"
#include "controller_action_router.h"
#include "controller_input.h"
#include "ota_update.h"
#include "platform/platform_power.h"

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
    crash_report_capture_boot();
    if (!controller_config_init()) {
        LOGE("configuration owner initialization failed");
        return;
    }

    /* One durable boot event for every physical target. Target power managers
     * only need to record the later attempt/preflight/entry boundaries. */
    platform_power_evidence_note_boot();

    /* Every target confirms a freshly installed image once it has stayed up. */
    ota_init();

    // Note: mDNS init moved to after WiFi connects (in main_idf.c)
    app_controller_init();
    bridge_client_start();
}
