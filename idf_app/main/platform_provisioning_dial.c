#include "platform/platform_provisioning.h"

#include "captive_portal.h"
#include "config_server.h"
#include "http_server_lifecycle.h"

bool platform_provisioning_start(void) {
    /* Both servers use port 80.  This transition must happen before Wi-Fi
     * announces AP readiness; waiting for RK_NET_EVT_AP_STARTED would make
     * the portal unable to bind and leave recovery stuck. */
    if (!http_server_lifecycle_lock()) {
        return false;
    }
    if (http_server_lifecycle_owner_locked() ==
            HTTP_SERVER_OWNER_CAPTIVE_PORTAL &&
        captive_portal_is_running_locked()) {
        http_server_lifecycle_unlock();
        return true;
    }

    /* Claim first: a deferred GOT_IP callback can no longer restart the
     * config server between its stop and the portal's port-80 bind. */
    http_server_lifecycle_claim_locked(HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
    config_server_stop_locked();
    bool ready = captive_portal_start_locked();
    if (!ready) {
        http_server_lifecycle_release_locked(
            HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
    }
    http_server_lifecycle_unlock();
    return ready;
}

void platform_provisioning_stop(void) {
    captive_portal_stop();
}
