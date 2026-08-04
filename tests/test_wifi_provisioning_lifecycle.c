#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "fake_esp_api.inc"
#include "wifi_manager.h"

static int s_ready_events;
static int s_failure_events;

static void *sta_start_actor(void *unused) {
    (void)unused;
    fixture_emit_sta_start();
    return NULL;
}

static void *start_provisioning_actor(void *result_out) {
    bool *result = result_out;
    *result = wifi_mgr_start_provisioning();
    return NULL;
}

static void *stop_ap_actor(void *unused) {
    (void)unused;
    wifi_mgr_stop_ap();
    return NULL;
}

static controller_config_wifi_snapshot_t wifi_projection(const char *ssid,
                                                          const char *pass) {
    controller_config_wifi_snapshot_t wifi = {
        .count = 1,
        .revision = 1,
    };
    rk_strlcpy(wifi.entries[0].ssid, ssid, sizeof(wifi.entries[0].ssid));
    rk_strlcpy(wifi.entries[0].pass, pass, sizeof(wifi.entries[0].pass));
    return wifi;
}

static void *apply_actor(void *unused) {
    (void)unused;
    controller_config_wifi_snapshot_t first =
        wifi_projection("actor-network-a", "actor-password-a");
    controller_config_wifi_snapshot_t second =
        wifi_projection("actor-network-b", "actor-password-b");
    for (int i = 0; i < 2000; ++i) {
        wifi_mgr_apply_wifi((i & 1) ? &first : &second, false);
    }
    return NULL;
}

static void *event_actor(void *unused) {
    (void)unused;
    for (int i = 0; i < 2000; ++i) {
        fixture_emit_sta_start();
        const char *ssid = fixture_wifi_ssid();
        const char *pass = fixture_wifi_pass();
        const bool first = strcmp(ssid, "actor-network-a") == 0 &&
                           strcmp(pass, "actor-password-a") == 0;
        const bool second = strcmp(ssid, "actor-network-b") == 0 &&
                            strcmp(pass, "actor-password-b") == 0;
        assert(first || second);
    }
    return NULL;
}

void rk_net_evt_cb(rk_net_evt_t evt, const char *detail) {
    if (evt == RK_NET_EVT_AP_STARTED) {
        assert(detail && strcmp(detail, "192.168.4.1") == 0);
        s_ready_events++;
    }
    if (evt == RK_NET_EVT_FAIL) {
        assert(detail && strcmp(detail, "Setup service unavailable; retrying") == 0);
        s_failure_events++;
    }
}

int main(void) {
    fixture_reset();
    wifi_mgr_start();

    controller_config_wifi_snapshot_t wifi = {
        .count = 1,
        .revision = 7,
    };
    rk_strlcpy(wifi.entries[0].ssid, "saved-network",
               sizeof(wifi.entries[0].ssid));
    rk_strlcpy(wifi.entries[0].pass, "saved-password",
               sizeof(wifi.entries[0].pass));
    wifi_mgr_apply_wifi(&wifi, false);
    assert(fixture_wifi_connect_calls() == 0);
    wifi_mgr_apply_wifi(&wifi, true);
    assert(fixture_wifi_connect_calls() == 1);
    assert(strcmp(fixture_wifi_ssid(), "saved-network") == 0);

    /* Configuration and event actors interleave: the next STA start must use
     * one coherent, newly-applied projection rather than a torn entry/index. */
    controller_config_wifi_snapshot_t replacement = wifi;
    rk_strlcpy(replacement.entries[0].ssid, "replacement-network",
               sizeof(replacement.entries[0].ssid));
    wifi_mgr_apply_wifi(&replacement, false);
    fixture_emit_sta_disconnected(WIFI_REASON_NO_AP_FOUND);
    fixture_emit_sta_start();
    assert(strcmp(fixture_wifi_ssid(), "replacement-network") == 0);

    /* Exercise the real configuration and ESP-event actors concurrently.
     * Every applied SSID/password pair must come from one complete copied
     * projection; a torn replacement fails this contract immediately. */
    controller_config_wifi_snapshot_t actor_start =
        wifi_projection("actor-network-a", "actor-password-a");
    wifi_mgr_apply_wifi(&actor_start, false);
    pthread_t apply_thread;
    pthread_t event_thread;
    assert(pthread_create(&apply_thread, NULL, apply_actor, NULL) == 0);
    assert(pthread_create(&event_thread, NULL, event_actor, NULL) == 0);
    assert(pthread_join(apply_thread, NULL) == 0);
    assert(pthread_join(event_thread, NULL) == 0);

    /* A STA connection actor paused in its hostname delay and an AP request
     * must serialize their ESP effects.  AP mode is the final effect; a stale
     * connect may not overtake it after the request wins the effect gate. */
    fixture_block_task_delay();
    pthread_t sta_thread;
    pthread_t ap_thread;
    bool ap_started = false;
    assert(pthread_create(&sta_thread, NULL, sta_start_actor, NULL) == 0);
    fixture_wait_task_delay_entered();
    assert(pthread_create(&ap_thread, NULL, start_provisioning_actor,
                          &ap_started) == 0);
    const struct timespec let_ap_contend = {
        .tv_nsec = 10 * 1000 * 1000,
    };
    nanosleep(&let_ap_contend, NULL);
    fixture_release_task_delay();
    assert(pthread_join(sta_thread, NULL) == 0);
    assert(pthread_join(ap_thread, NULL) == 0);
    assert(ap_started);
    assert(fixture_last_connect_sequence() < fixture_ap_mode_sequence());
    assert(wifi_mgr_is_ap_mode());
    assert(fixture_provisioning_start_calls() == 1);
    assert(fixture_provisioning_stop_calls() == 1);
    assert(s_ready_events == 0);
    assert(s_failure_events == 1);
    assert(fixture_timer_active("provisioning_retry"));
    assert(fixture_timer_delay_us("provisioning_retry") == 500000);

    fixture_timer_fire("provisioning_retry");
    assert(fixture_provisioning_start_calls() == 2);
    assert(s_ready_events == 0);
    assert(s_failure_events == 2);
    assert(fixture_timer_active("provisioning_retry"));
    assert(fixture_timer_delay_us("provisioning_retry") == 1000000);

    fixture_set_provisioning_ready(true);
    fixture_timer_fire("provisioning_retry");
    assert(fixture_provisioning_start_calls() == 3);
    assert(s_ready_events == 1);
    assert(!fixture_timer_active("provisioning_retry"));

    assert(wifi_mgr_start_provisioning());
    assert(fixture_provisioning_start_calls() == 3);
    assert(s_ready_events == 1);

    wifi_mgr_stop_ap();
    assert(fixture_provisioning_stop_calls() == 3);

    fixture_set_provisioning_ready(false);
    assert(wifi_mgr_start_provisioning());
    assert(fixture_timer_active("provisioning_retry"));
    int starts_before_stop = fixture_provisioning_start_calls();
    wifi_mgr_stop_ap();
    assert(!fixture_timer_active("provisioning_retry"));
    fixture_timer_fire("provisioning_retry");
    assert(fixture_provisioning_start_calls() == starts_before_stop);

    /* Target provisioning start and stop are separate actors.  Force start
     * to remain in-flight while AP exit is requested and prove the serialized
     * effect boundary prevents their target operations from overlapping. */
    fixture_set_provisioning_ready(true);
    fixture_block_provisioning_start();
    ap_started = false;
    assert(pthread_create(&ap_thread, NULL, start_provisioning_actor,
                          &ap_started) == 0);
    fixture_wait_provisioning_start_entered();
    pthread_t stop_thread;
    assert(pthread_create(&stop_thread, NULL, stop_ap_actor, NULL) == 0);
    fixture_release_provisioning_start();
    assert(pthread_join(ap_thread, NULL) == 0);
    assert(pthread_join(stop_thread, NULL) == 0);
    assert(ap_started);
    assert(!wifi_mgr_is_ap_mode());
    assert(!fixture_provisioning_effects_overlapped());

    wifi_mgr_stop();
    return 0;
}
