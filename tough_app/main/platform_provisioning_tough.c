#include "platform/platform_provisioning.h"

#include "captive_portal.h"

bool platform_provisioning_start(void) {
    /* The target implementation holds one lock while replacing STA mode,
     * binding AP mode, and registering every required handler. */
    return captive_portal_start();
}

void platform_provisioning_stop(void) {
    captive_portal_stop();
}
