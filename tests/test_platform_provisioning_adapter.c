#include <assert.h>
#include <stdbool.h>

#include "platform/platform_provisioning.h"

#if !defined(TEST_DIAL_ADAPTER) && !defined(TEST_FRAME_ADAPTER)
#error "Select the adapter under test"
#endif

static bool s_portal_start_succeeds;
static bool s_portal_running;
#if defined(TEST_FRAME_ADAPTER)
static bool s_sta_server_running;
#else
static bool s_config_server_running;
#endif
static int s_portal_start_calls;
static int s_portal_stop_calls;
static int s_config_server_stop_calls;

#if defined(TEST_DIAL_ADAPTER)
#include "http_server_lifecycle.h"

static int s_config_restart_suppressed;

static void simulate_deferred_config_restart(void) {
    assert(http_server_lifecycle_owner_locked() ==
           HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
    s_config_restart_suppressed++;
}

void config_server_stop_locked(void) {
    if (s_config_server_running) {
        s_config_server_stop_calls++;
        s_config_server_running = false;
        /* Model a deferred GOT_IP start exactly in the handoff window.  The
         * AP claim must already be visible, so the restart is suppressed. */
        simulate_deferred_config_restart();
    }
}

bool captive_portal_start_locked(void) {
    assert(http_server_lifecycle_owner_locked() ==
           HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
    assert(!s_config_server_running);
    s_portal_start_calls++;
    s_portal_running = s_portal_start_succeeds;
    return s_portal_running;
}

bool captive_portal_is_running_locked(void) {
    return s_portal_running;
}

void captive_portal_stop(void) {
    assert(http_server_lifecycle_lock());
    s_portal_stop_calls++;
    s_portal_running = false;
    http_server_lifecycle_release_locked(HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
    http_server_lifecycle_unlock();
}

#else

bool captive_portal_start(void) {
    if (s_portal_running) {
        return true;
    }
    /* Frame performs the STA-to-AP replacement inside captive_portal_start's
     * lifecycle critical section. */
    if (s_sta_server_running) {
        s_portal_stop_calls++;
        s_sta_server_running = false;
    }
    s_portal_start_calls++;
    s_portal_running = s_portal_start_succeeds;
    return s_portal_running;
}

void captive_portal_stop(void) {
    s_portal_stop_calls++;
    s_portal_running = false;
    s_sta_server_running = false;
}

#endif

int main(void) {
    /* A failed AP start must not be reported as readiness. */
    s_portal_start_succeeds = false;
    assert(!platform_provisioning_start());
    assert(s_portal_start_calls == 1);
    assert(!s_portal_running);

#if defined(TEST_DIAL_ADAPTER)
    s_config_server_running = true;
#else
    s_sta_server_running = true;
#endif
    s_portal_start_succeeds = true;
    assert(platform_provisioning_start());
    assert(s_portal_start_calls == 2);
    assert(s_portal_running);

#if defined(TEST_DIAL_ADAPTER)
    assert(s_config_server_stop_calls == 1);
    assert(s_config_restart_suppressed == 1);
    assert(s_portal_stop_calls == 0);
#else
    assert(s_config_server_stop_calls == 0);
    assert(s_portal_stop_calls == 1);
    assert(!s_sta_server_running);
#endif

    /* Repeated AP-ready checks are idempotent and never tear down the portal. */
    assert(platform_provisioning_start());
    assert(s_portal_start_calls == 2);

    platform_provisioning_stop();
#if defined(TEST_DIAL_ADAPTER)
    assert(s_portal_stop_calls == 1);
#else
    assert(s_portal_stop_calls == 2);
#endif
    assert(!s_portal_running);
    return 0;
}
