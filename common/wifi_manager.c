#include "wifi_manager.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <string.h>

#include "controller_config.h"
#include "os_mutex.h"
#include "platform/platform_identity.h"
#include "platform/platform_provisioning.h"

static const char *TAG = "wifi_mgr";
static const uint32_t s_backoff_ms[] = {500, 1000, 2000, 4000, 8000, 16000, 30000};
static const uint32_t s_provisioning_retry_ms[] = {500, 1000, 2000, 4000, 8000, 16000, 30000};
static const char *s_last_error = NULL;  // Last disconnect reason for UI display

// Map WiFi disconnect reason to human-readable string and event type
static const char *get_disconnect_reason_str(uint8_t reason, rk_net_evt_t *out_evt) {
    rk_net_evt_t evt = RK_NET_EVT_FAIL;  // Default
    const char *str;

    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
            str = "Network not found";
            evt = RK_NET_EVT_NO_AP_FOUND;
            break;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_MIC_FAILURE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            str = "Wrong password";
            evt = RK_NET_EVT_WRONG_PASSWORD;
            break;
        case WIFI_REASON_AUTH_EXPIRE:
            str = "Auth expired";
            evt = RK_NET_EVT_AUTH_TIMEOUT;
            break;
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_ASSOC_EXPIRE:
            str = "Association failed";
            break;
        case WIFI_REASON_BEACON_TIMEOUT:
            str = "Beacon timeout (out of range?)";
            break;
        case WIFI_REASON_ASSOC_LEAVE:
            str = "Disconnected by AP";
            break;
        case WIFI_REASON_CONNECTION_FAIL:
            str = "Connection failed";
            break;
        case WIFI_REASON_AP_TSF_RESET:
            str = "AP reset";
            break;
        default:
            str = "Unknown error";
            break;
    }

    if (out_evt) {
        *out_evt = evt;
    }
    return str;
}

// AP mode configuration
#define AP_MAX_CONNECTIONS 2
#define STA_FAIL_THRESHOLD 5  // Switch to AP after this many consecutive STA failures

/* Wi-Fi owns only a copied runtime projection. Persistence stays in
 * controller_config. */
static controller_config_wifi_snapshot_t s_wifi_cfg;
/*
 * Wi-Fi is driven by three independent actors: configuration requests,
 * esp_event callbacks, and esp_timer callbacks.  This lock owns their shared
 * projection and transition state.  Never hold it while calling ESP-IDF,
 * controller_config, a target provisioning adapter, or rk_net_evt_cb: each of
 * those can synchronously re-enter one of the actors.
 */
static os_mutex_t s_wifi_state_lock = OS_MUTEX_INITIALIZER;
/* ESP Wi-Fi lifecycle effects and target provisioning effects have distinct
 * gates.  ESP event callbacks never take s_wifi_effect_lock directly. */
static os_mutex_t s_wifi_effect_lock = OS_MUTEX_INITIALIZER;
static os_mutex_t s_provisioning_effect_lock = OS_MUTEX_INITIALIZER;
static bool s_wifi_cfg_loaded;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_provisioning_retry_timer;
static size_t s_backoff_idx;
static size_t s_provisioning_retry_idx;
static bool s_started;
/* True only after ESP WiFi can accept a provisioning mode transition. */
static bool s_provisioning_ready;
/* True only after the target provisioning service has reported ready. */
static bool s_provisioning_service_ready;
static bool s_provisioning_service_starting;
static char s_ip[16];
static bool s_ap_mode;           // true when in AP provisioning mode
static bool s_wifi_mode_transitioning;
static int s_sta_fail_count;     // consecutive STA connection failures
static char s_device_hostname[32] = {0};  // cached network hostname
static int s_wifi_idx;           // index into this device's saved WiFi list

#ifdef ESP_PLATFORM
/* os_mutex_lock() provides convenient lazy allocation on ESP, but that
 * check/create sequence is not a one-time initializer when two actors arrive
 * together.  Create every Wi-Fi lock under one critical section and use
 * static storage before any actor attempts to take one. */
static portMUX_TYPE s_wifi_locks_init = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_wifi_state_lock_storage;
static StaticSemaphore_t s_wifi_effect_lock_storage;
static StaticSemaphore_t s_provisioning_effect_lock_storage;

static bool ensure_wifi_locks(void) {
    bool ready;
    taskENTER_CRITICAL(&s_wifi_locks_init);
    if (s_wifi_state_lock == NULL) {
        s_wifi_state_lock =
            xSemaphoreCreateMutexStatic(&s_wifi_state_lock_storage);
    }
    if (s_wifi_effect_lock == NULL) {
        s_wifi_effect_lock =
            xSemaphoreCreateMutexStatic(&s_wifi_effect_lock_storage);
    }
    if (s_provisioning_effect_lock == NULL) {
        s_provisioning_effect_lock =
            xSemaphoreCreateMutexStatic(&s_provisioning_effect_lock_storage);
    }
    ready = s_wifi_state_lock != NULL && s_wifi_effect_lock != NULL &&
            s_provisioning_effect_lock != NULL;
    taskEXIT_CRITICAL(&s_wifi_locks_init);
    return ready;
}
#else
static bool ensure_wifi_locks(void) {
    return true;
}
#endif

static bool lock_wifi_state(void) {
    return ensure_wifi_locks() && os_mutex_lock(&s_wifi_state_lock) == 0;
}

static void unlock_wifi_state(void) {
    (void)os_mutex_unlock(&s_wifi_state_lock);
}

static bool lock_wifi_effect(void) {
    return ensure_wifi_locks() && os_mutex_lock(&s_wifi_effect_lock) == 0;
}

static void unlock_wifi_effect(void) {
    (void)os_mutex_unlock(&s_wifi_effect_lock);
}

static bool lock_provisioning_effect(void) {
    return ensure_wifi_locks() &&
           os_mutex_lock(&s_provisioning_effect_lock) == 0;
}

static void unlock_provisioning_effect(void) {
    (void)os_mutex_unlock(&s_provisioning_effect_lock);
}

static void provisioning_stop_effect(void) {
    if (!lock_provisioning_effect()) {
        return;
    }
    platform_provisioning_stop();
    unlock_provisioning_effect();
}

static void copy_str(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    size_t in_len = 0;
    while (in_len + 1 < dst_len && src[in_len] != '\0') {
        ++in_len;
    }
    memcpy(dst, src, in_len);
    dst[in_len] = '\0';
}

// Sanitize hostname: only alphanumeric + hyphen, convert to lowercase
static void sanitize_hostname(const char *input, char *output, size_t output_len) {
    size_t j = 0;
    for (size_t i = 0; input[i] && j < output_len - 1; i++) {
        char c = input[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            output[j++] = c;
        } else if (c >= 'A' && c <= 'Z') {
            output[j++] = c + 32;  // Convert to lowercase
        } else if (c == ' ' || c == '_') {
            output[j++] = '-';  // Space/underscore becomes hyphen
        }
        // All other chars are dropped
    }
    output[j] = '\0';

    // Trim leading hyphens
    size_t start = 0;
    while (output[start] == '-') {
        start++;
    }
    if (start > 0) {
        memmove(output, output + start, j - start + 1);
        j -= start;
    }

    // Trim trailing hyphens
    while (j > 0 && output[j - 1] == '-') {
        output[--j] = '\0';
    }

    // If sanitization resulted in empty string, use fallback
    if (j == 0) {
        snprintf(output, output_len, "%s", platform_device_slug());
    }
}

// Get device hostname (cached, generated once per boot)
// Priority: bridge-configured knob_name → MAC-based → platform default slug
static const char *get_device_hostname(void) {
    // Cache to avoid regenerating on every call
    if (s_device_hostname[0] != '\0') {
        return s_device_hostname;
    }

    // If the server has set a custom name, use a copied owner snapshot.
    controller_config_snapshot_t config;
    if (controller_config_snapshot(&config) && config.value.knob_name[0] != '\0') {
        sanitize_hostname(config.value.knob_name, s_device_hostname,
                          sizeof(s_device_hostname));
        ESP_LOGI(TAG, "Hostname from bridge config: %s", s_device_hostname);
        return s_device_hostname;
    }

    // Fallback to MAC-based hostname (last 3 bytes for uniqueness)
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read MAC for hostname: %s", esp_err_to_name(err));
        snprintf(s_device_hostname, sizeof(s_device_hostname), "%s",
                 platform_device_slug());
        return s_device_hostname;
    }

    snprintf(s_device_hostname, sizeof(s_device_hostname), "%s-%02x%02x%02x",
             platform_device_slug(), mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Generated MAC-based hostname: %s", s_device_hostname);
    return s_device_hostname;
}

/* Caller holds s_wifi_state_lock. */
static const rk_wifi_entry_t *active_wifi_locked(void) {
    if (!s_wifi_cfg_loaded || s_wifi_idx < 0 ||
        s_wifi_idx >= s_wifi_cfg.count || s_wifi_idx >= RK_MAX_WIFI) {
        return NULL;
    }
    return &s_wifi_cfg.entries[s_wifi_idx];
}

static void ensure_wifi_loaded(void) {
    if (!lock_wifi_state()) {
        return;
    }
    const bool already_loaded = s_wifi_cfg_loaded;
    unlock_wifi_state();
    if (already_loaded) {
        return;
    }

    controller_config_wifi_snapshot_t wifi = {0};
    if (!controller_config_wifi_snapshot(&wifi)) {
        ESP_LOGE(TAG, "Controller configuration unavailable; using setup AP recovery");
    }
    if (!lock_wifi_state()) {
        return;
    }
    /* Do not replace a projection applied while the owner snapshot was read. */
    if (!s_wifi_cfg_loaded) {
        s_wifi_idx = 0;
        s_wifi_cfg = wifi;
        s_wifi_cfg_loaded = true;
    }
    unlock_wifi_state();
}

static bool active_wifi_copy(rk_wifi_entry_t *out) {
    if (!out) {
        return false;
    }
    ensure_wifi_loaded();
    if (!lock_wifi_state()) {
        return false;
    }
    const rk_wifi_entry_t *active = active_wifi_locked();
    const bool found = active != NULL;
    if (active) {
        *out = *active;
    }
    unlock_wifi_state();
    return found;
}

/* Caller holds s_wifi_effect_lock and supplies an immutable state snapshot. */
static esp_err_t apply_wifi_config(const rk_wifi_entry_t *active) {
    wifi_config_t cfg = {0};
    copy_str((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid),
             active ? active->ssid : "");
    copy_str((char *)cfg.sta.password, sizeof(cfg.sta.password),
             active ? active->pass : "");
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static void reset_backoff(void) {
    if (!lock_wifi_state()) {
        return;
    }
    s_backoff_idx = 0;
    unlock_wifi_state();
}

static void schedule_retry_with_reason(uint8_t reason);
static void schedule_retry(void);
static void start_ap_mode(void);
static void start_provisioning_service(void);
static void cancel_provisioning_retry(void);

static void connect_now(void) {
    ensure_wifi_loaded();
    const char *hostname = get_device_hostname();

    if (!lock_wifi_effect()) {
        return;
    }
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    const bool can_connect = s_started && !s_ap_mode &&
                             !s_wifi_mode_transitioning;
    const rk_wifi_entry_t *current = active_wifi_locked();
    const bool have_active = current != NULL;
    const esp_timer_handle_t retry_timer = s_retry_timer;
    rk_wifi_entry_t active = {0};
    if (current) {
        active = *current;
    }
    unlock_wifi_state();
    if (!can_connect) {
        unlock_wifi_effect();
        return;
    }
    if (!have_active || active.ssid[0] == '\0') {
        unlock_wifi_effect();
        ESP_LOGW(TAG, "SSID empty; starting AP mode for provisioning");
        start_ap_mode();
        return;
    }
    // Set hostname before connection (with delay per Arduino pattern)
    esp_netif_set_hostname(s_sta_netif, hostname);
    vTaskDelay(pdMS_TO_TICKS(100));  // Delay to let hostname settle
    ESP_LOGI(TAG, "Hostname set before connect: %s", hostname);

    /* AP/stop requests can update intent while the hostname delay runs.  The
     * Wi-Fi effect gate prevents their hardware transition from overtaking
     * this actor, but this second state check keeps a stale STA connect from
     * being issued after that newer intent was recorded. */
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    const bool still_can_connect = s_started && !s_ap_mode &&
                                   !s_wifi_mode_transitioning;
    unlock_wifi_state();
    if (!still_can_connect) {
        unlock_wifi_effect();
        return;
    }

    ESP_LOGI(TAG, "Connecting to WiFi SSID: '%s'", active.ssid);
    if (retry_timer) {
        esp_timer_stop(retry_timer);
    }
    if (apply_wifi_config(&active) != ESP_OK) {
        unlock_wifi_effect();
        ESP_LOGE(TAG, "failed to apply Wi-Fi config");
        schedule_retry();
        return;
    }
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "disconnect failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_connect();
    unlock_wifi_effect();
    rk_net_evt_cb(RK_NET_EVT_CONNECTING, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect failed: %s", esp_err_to_name(err));
        schedule_retry();
    }
}

static void retry_timer_cb(void *arg) {
    (void)arg;
    connect_now();
}

static void schedule_provisioning_retry(void) {
    if (!lock_wifi_state()) {
        return;
    }
    if (!s_ap_mode || s_provisioning_service_ready ||
        s_provisioning_service_starting) {
        unlock_wifi_state();
        return;
    }

    const uint32_t delay = s_provisioning_retry_ms[s_provisioning_retry_idx];
    if (s_provisioning_retry_idx + 1 <
        (sizeof(s_provisioning_retry_ms) / sizeof(s_provisioning_retry_ms[0]))) {
        s_provisioning_retry_idx++;
    }

    const esp_timer_handle_t retry_timer = s_provisioning_retry_timer;
    unlock_wifi_state();
    if (!retry_timer) {
        ESP_LOGE(TAG, "provisioning retry timer missing");
        return;
    }

    esp_timer_stop(retry_timer);
    esp_err_t err = esp_timer_start_once(retry_timer, delay * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "provisioning retry timer start failed: %s", esp_err_to_name(err));
    }
}

static void cancel_provisioning_retry(void) {
    if (!lock_wifi_state()) {
        return;
    }
    const esp_timer_handle_t retry_timer = s_provisioning_retry_timer;
    s_provisioning_retry_idx = 0;
    unlock_wifi_state();
    if (retry_timer) {
        esp_timer_stop(retry_timer);
    }
}

static void provisioning_retry_timer_cb(void *arg) {
    (void)arg;
    start_provisioning_service();
}

static void start_provisioning_service(void) {
    if (!lock_wifi_state()) {
        return;
    }
    if (!s_ap_mode || s_provisioning_service_ready ||
        s_provisioning_service_starting) {
        unlock_wifi_state();
        return;
    }
    s_provisioning_service_starting = true;
    unlock_wifi_state();

    /* Keep target start, acceptance, and any compensating stop in one effect
     * critical section.  A concurrent AP exit can update state, but cannot
     * overlap target HTTP/DNS teardown with target startup. */
    if (!lock_provisioning_effect()) {
        if (lock_wifi_state()) {
            s_provisioning_service_starting = false;
            unlock_wifi_state();
        }
        schedule_provisioning_retry();
        return;
    }
    const bool started = platform_provisioning_start();
    if (!lock_wifi_state()) {
        platform_provisioning_stop();
        unlock_provisioning_effect();
        return;
    }
    s_provisioning_service_starting = false;
    const bool still_ap_mode = s_ap_mode;
    if (started && still_ap_mode) {
        s_provisioning_service_ready = true;
        s_provisioning_retry_idx = 0;
        unlock_wifi_state();
        unlock_provisioning_effect();
        ESP_LOGI(TAG, "Provisioning service ready");
        rk_net_evt_cb(RK_NET_EVT_AP_STARTED, "192.168.4.1");
        return;
    }
    unlock_wifi_state();

    /* The target may have partially initialized before reporting failure. */
    platform_provisioning_stop();
    unlock_provisioning_effect();
    if (!still_ap_mode) {
        return;
    }
    ESP_LOGE(TAG, "Provisioning service unavailable; retrying while AP remains active");
    rk_net_evt_cb(RK_NET_EVT_FAIL, "Setup service unavailable; retrying");
    schedule_provisioning_retry();
}

static void schedule_retry_with_reason(uint8_t reason) {
    if (!lock_wifi_state()) {
        return;
    }
    if (s_ap_mode) {
        unlock_wifi_state();
        return;
    }
    s_sta_fail_count++;

    // Get human-readable reason and specific event type
    rk_net_evt_t evt = RK_NET_EVT_FAIL;
    s_last_error = get_disconnect_reason_str(reason, &evt);

    const int fail_count = s_sta_fail_count;
    const char *last_error = s_last_error;
    (void)fail_count;  // Native fixture compiles ESP_LOGW out.

    // Try each network saved on this device before entering provisioning mode.
    if (s_sta_fail_count >= STA_FAIL_THRESHOLD) {
        const rk_wifi_entry_t *prior = active_wifi_locked();
        char prior_ssid[sizeof(((rk_wifi_entry_t *)0)->ssid)] = {0};
        copy_str(prior_ssid, sizeof(prior_ssid), prior ? prior->ssid : "");
        s_wifi_idx++;
        if (s_wifi_idx < s_wifi_cfg.count && s_wifi_idx < RK_MAX_WIFI) {
            char next_ssid[sizeof(((rk_wifi_entry_t *)0)->ssid)] = {0};
            copy_str(next_ssid, sizeof(next_ssid), s_wifi_cfg.entries[s_wifi_idx].ssid);
            s_sta_fail_count = 0;
            s_backoff_idx = 0;
            unlock_wifi_state();
            ESP_LOGW(TAG, "WiFi '%s' failed %d times; trying saved network '%s'",
                     prior_ssid,
                     STA_FAIL_THRESHOLD,
                     next_ssid);
            connect_now();
            return;
        }
        const size_t wifi_count = s_wifi_cfg.count;
        (void)wifi_count;  // Native fixture compiles ESP_LOGW out.
        unlock_wifi_state();
        ESP_LOGW(TAG, "All %d saved WiFi networks failed; starting provisioning",
                 (int)wifi_count);
        start_ap_mode();
        return;
    }

    uint32_t delay = s_backoff_ms[s_backoff_idx];
    if (s_backoff_idx + 1 < (sizeof(s_backoff_ms) / sizeof(s_backoff_ms[0]))) {
        s_backoff_idx++;
    }
    const esp_timer_handle_t retry_timer = s_retry_timer;
    unlock_wifi_state();
    ESP_LOGW(TAG, "WiFi disconnected: %s (reason %d, attempt %d/%d)",
             last_error, reason, fail_count, STA_FAIL_THRESHOLD);
    if (retry_timer) {
        esp_timer_stop(retry_timer);
        esp_err_t err = esp_timer_start_once(retry_timer, delay * 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "retry timer start failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "retry timer missing; reconnect immediately");
        connect_now();
    }
    // Emit specific event for UI (e.g., RK_NET_EVT_WRONG_PASSWORD)
    rk_net_evt_cb(evt, last_error);
}

static void schedule_retry(void) {
    // Called when we don't have a specific reason (e.g., config apply failed)
    schedule_retry_with_reason(0);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        reset_backoff();
        if (lock_wifi_state()) {
            s_last_error = NULL;  // Clear last error on new connection attempt
            unlock_wifi_state();
        }
        connect_now();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Extract disconnect reason from event data
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = disconn ? disconn->reason : 0;
        schedule_retry_with_reason(reason);
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)event_data;
    esp_ip4_addr_t ip = evt->ip_info.ip;
    char ip_text[sizeof(s_ip)] = {0};
    esp_ip4addr_ntoa(&ip, ip_text, sizeof(ip_text));

    // Debug: Verify hostname persists after connection
    const char *check_hostname = NULL;
    esp_netif_get_hostname(s_sta_netif, &check_hostname);
    rk_wifi_entry_t active = {0};
    const bool have_active = active_wifi_copy(&active);
    (void)have_active;  // Native fixture compiles ESP_LOGI out.
    ESP_LOGI(TAG, "Connected to WiFi SSID: '%s', IP: %s, hostname: %s",
             have_active ? active.ssid : "", ip_text,
             check_hostname ? check_hostname : "NULL");

    // Re-assert hostname after IP acquisition to force DHCP INFORM
    // Some routers (UniFi) may need this to solidify the hostname
    const char *hostname = get_device_hostname();
    esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Re-asserted hostname after IP acquisition: %s", hostname);
    }

    if (lock_wifi_state()) {
        copy_str(s_ip, sizeof(s_ip), ip_text);
        s_backoff_idx = 0;
        s_sta_fail_count = 0;  // Reset failure count on successful connection
        s_last_error = NULL;   // Clear last error on success
        unlock_wifi_state();
    }
    rk_net_evt_cb(RK_NET_EVT_GOT_IP, ip_text);
}

static void start_ap_mode(void) {
    if (!lock_wifi_effect()) {
        return;
    }
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    if (s_ap_mode) {
        unlock_wifi_state();
        unlock_wifi_effect();
        start_provisioning_service();
        return;
    }

    // Mark the transition before stopping STA. esp_wifi_stop() can emit a
    // disconnect event synchronously; the event handler must not schedule
    // another retry/AP transition while this one is in progress.
    const esp_timer_handle_t provisioning_retry_timer = s_provisioning_retry_timer;
    s_provisioning_ready = false;
    s_ap_mode = true;
    s_provisioning_service_ready = false;
    s_provisioning_service_starting = false;
    s_provisioning_retry_idx = 0;
    s_wifi_mode_transitioning = true;
    unlock_wifi_state();
    if (provisioning_retry_timer) {
        esp_timer_stop(provisioning_retry_timer);
    }
    const char *ap_ssid = platform_provisioning_ssid();
    ESP_LOGI(TAG, "Starting AP mode for provisioning (SSID: %s)", ap_ssid);
    esp_wifi_stop();

    // Create AP netif if needed
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();

        // Set hostname for AP mode too (prevents "espressif" from appearing)
        const char *hostname = get_device_hostname();
        esp_err_t err = esp_netif_set_hostname(s_ap_netif, hostname);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "AP mode hostname set: %s", hostname);
        }
    }

    // Configure AP with optimal settings for discoverability
    wifi_config_t ap_config = {0};
    copy_str((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), ap_ssid);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = 6;
    ap_config.ap.max_connection = AP_MAX_CONNECTIONS;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.beacon_interval = 100;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (lock_wifi_state()) {
        s_provisioning_ready = true;
        s_wifi_mode_transitioning = false;
        s_sta_fail_count = 0;
        unlock_wifi_state();
    }

    // Use maximum TX power in AP mode for better discoverability
    // (We reduce power in STA mode for battery, but setup needs visibility)
    // Units are 0.25 dBm, so 80 = 20 dBm (max)
    esp_err_t tx_err = esp_wifi_set_max_tx_power(80);
    if (tx_err == ESP_OK) {
        ESP_LOGI(TAG, "AP mode: TX power set to 20 dBm for better discoverability");
    } else {
        ESP_LOGW(TAG, "AP mode: Could not set TX power: %s", esp_err_to_name(tx_err));
    }

    unlock_wifi_effect();
    start_provisioning_service();
}

bool wifi_mgr_start_provisioning(void) {
    if (!lock_wifi_state()) {
        return false;
    }
    const bool ready = s_started && s_provisioning_ready;
    unlock_wifi_state();
    if (!ready) {
        ESP_LOGW(TAG, "Provisioning requested before WiFi manager is ready");
        return false;
    }
    start_ap_mode();
    return true;
}

void wifi_mgr_start(void) {
    if (!lock_wifi_effect()) {
        return;
    }
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    if (s_started) {
        unlock_wifi_state();
        unlock_wifi_effect();
        return;
    }
    s_provisioning_ready = false;
    s_provisioning_service_ready = false;
    s_provisioning_service_starting = false;
    s_provisioning_retry_idx = 0;
    s_wifi_mode_transitioning = true;
    s_started = true;
    unlock_wifi_state();

    ensure_wifi_loaded();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        if (lock_wifi_state()) {
            s_started = false;
            s_wifi_mode_transitioning = false;
            unlock_wifi_state();
        }
        unlock_wifi_effect();
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        if (lock_wifi_state()) {
            s_started = false;
            s_wifi_mode_transitioning = false;
            unlock_wifi_state();
        }
        unlock_wifi_effect();
        return;
    }
    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();

        // Set DHCP hostname BEFORE WiFi starts (for router UI visibility)
        const char *hostname = get_device_hostname();
        esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DHCP hostname set: %s", hostname);
        } else {
            ESP_LOGW(TAG, "Failed to set DHCP hostname: %s (continuing)", esp_err_to_name(err));
        }
    }

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // Disable WiFi power save for reliable HTTP polling
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    const esp_timer_create_args_t retry_args = {
        .callback = &retry_timer_cb,
        .name = "wifi_retry",
    };
    esp_timer_handle_t retry_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &retry_timer));
    const esp_timer_create_args_t provisioning_retry_args = {
        .callback = &provisioning_retry_timer_cb,
        .name = "provisioning_retry",
    };
    esp_timer_handle_t provisioning_retry_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&provisioning_retry_args,
                                     &provisioning_retry_timer));
    if (lock_wifi_state()) {
        s_retry_timer = retry_timer;
        s_provisioning_retry_timer = provisioning_retry_timer;
        unlock_wifi_state();
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    if (lock_wifi_state()) {
        s_provisioning_ready = true;
        s_wifi_mode_transitioning = false;
        unlock_wifi_state();
    }

    // Reduce WiFi TX power for battery operation (11 dBm instead of 20 dBm)
    // This reduces peak current from ~500mA to ~200mA during WiFi transmission
    // Units are 0.25 dBm, so 44 = 11 dBm
    // Note: May fail if WiFi not fully started (AP mode), so don't use ESP_ERROR_CHECK
    esp_err_t tx_err = esp_wifi_set_max_tx_power(44);
    if (tx_err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi TX power reduced to 11 dBm for battery compatibility");
    } else {
        ESP_LOGW(TAG, "Could not set WiFi TX power: %s (will use default)", esp_err_to_name(tx_err));
    }
    unlock_wifi_effect();
}

void wifi_mgr_apply_wifi(const controller_config_wifi_snapshot_t *wifi,
                         bool reconnect) {
    if (!wifi || wifi->count > RK_MAX_WIFI) {
        ESP_LOGW(TAG, "Ignoring invalid Wi-Fi projection");
        return;
    }

    if (!lock_wifi_state()) {
        return;
    }
    s_wifi_cfg = *wifi;
    s_wifi_cfg_loaded = true;
    s_wifi_idx = 0;
    s_backoff_idx = 0;
    s_sta_fail_count = 0;  // Reset failure count for new credentials
    const bool started = s_started;
    const bool ap_mode = s_ap_mode;
    unlock_wifi_state();

    if (!started) {
        ESP_LOGI(TAG, "Applied Wi-Fi projection before manager start");
        return;
    }

    if (!reconnect) {
        return;
    }

    // If in AP mode, stop it first before connecting
    if (ap_mode) {
        ESP_LOGI(TAG, "Stopping AP mode to connect with new credentials");
        wifi_mgr_stop_ap();
        // wifi_mgr_stop_ap switches to STA and triggers connect via event
    } else {
        connect_now();
    }
}

void wifi_mgr_forget_wifi(void) {
    ESP_LOGW(TAG, "Factory reset requested - erasing NVS and rebooting");

    // Stop WiFi first
    if (!lock_wifi_effect()) {
        return;
    }
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    s_provisioning_ready = false;
    s_provisioning_service_ready = false;
    s_provisioning_service_starting = false;
    s_wifi_mode_transitioning = true;
    const bool started = s_started;
    unlock_wifi_state();
    cancel_provisioning_retry();
    provisioning_stop_effect();
    if (started) {
        esp_wifi_stop();
    }
    unlock_wifi_effect();

    // Erase all NVS data (WiFi credentials, config, everything)
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
    }

    // Reboot - device will start fresh with captive portal
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
    // Never returns
}

bool wifi_mgr_get_ip(char *buf, size_t n) {
    if (!buf || n == 0) {
        return false;
    }
    if (!lock_wifi_state()) {
        buf[0] = '\0';
        return false;
    }
    if (s_ip[0] == '\0') {
        buf[0] = '\0';
        unlock_wifi_state();
        return false;
    }
    copy_str(buf, n, s_ip);
    unlock_wifi_state();
    return true;
}

void wifi_mgr_get_ssid(char *buf, size_t n) {
    if (!buf || n == 0) {
        return;
    }
    rk_wifi_entry_t active = {0};
    const bool have_active = active_wifi_copy(&active);
    copy_str(buf, n, have_active ? active.ssid : "");
}

bool wifi_mgr_is_ap_mode(void) {
    if (!lock_wifi_state()) {
        return false;
    }
    const bool ap_mode = s_ap_mode;
    unlock_wifi_state();
    return ap_mode;
}

const char *wifi_mgr_get_last_error(void) {
    if (!lock_wifi_state()) {
        return NULL;
    }
    const char *last_error = s_last_error;
    unlock_wifi_state();
    return last_error;
}

int wifi_mgr_get_retry_count(void) {
    if (!lock_wifi_state()) {
        return 0;
    }
    const int retry_count = s_sta_fail_count;
    unlock_wifi_state();
    return retry_count;
}

int wifi_mgr_get_retry_max(void) {
    return STA_FAIL_THRESHOLD;
}

void wifi_mgr_stop(void) {
    if (!lock_wifi_effect()) {
        return;
    }
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    const bool started = s_started;
    const esp_timer_handle_t retry_timer = s_retry_timer;
    s_provisioning_ready = false;
    s_provisioning_service_ready = false;
    s_provisioning_service_starting = false;
    s_started = false;
    s_ap_mode = false;
    s_wifi_mode_transitioning = true;
    s_sta_fail_count = 0;
    s_wifi_idx = 0;
    s_ip[0] = '\0';
    unlock_wifi_state();
    cancel_provisioning_retry();
    provisioning_stop_effect();
    if (!started) {
        if (lock_wifi_state()) {
            s_wifi_mode_transitioning = false;
            unlock_wifi_state();
        }
        unlock_wifi_effect();
        return;
    }

    ESP_LOGI(TAG, "Stopping WiFi completely (for BLE mode)");

    // Unregister event handlers FIRST to prevent reconnect attempts
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler);

    // Stop retry timer
    if (retry_timer) {
        esp_timer_stop(retry_timer);
    }

    // Stop WiFi
    esp_wifi_stop();
    esp_wifi_deinit();

    if (lock_wifi_state()) {
        s_wifi_mode_transitioning = false;
        unlock_wifi_state();
    }
    unlock_wifi_effect();

    ESP_LOGI(TAG, "WiFi stopped");
}

void wifi_mgr_stop_ap(void) {
    if (!lock_wifi_effect()) {
        return;
    }
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    if (!s_ap_mode) {
        unlock_wifi_state();
        unlock_wifi_effect();
        return;
    }

    s_provisioning_ready = false;
    s_provisioning_service_ready = false;
    s_provisioning_service_starting = false;
    s_ap_mode = false;
    s_wifi_mode_transitioning = true;
    s_sta_fail_count = 0;
    s_wifi_idx = 0;
    s_ip[0] = '\0';
    unlock_wifi_state();

    ESP_LOGI(TAG, "Stopping AP mode, switching to STA");

    cancel_provisioning_retry();
    provisioning_stop_effect();
    // Stop AP
    esp_wifi_stop();

    // Switch to STA mode. The STA_START event may queue while this transition
    // owns the effect gate; it will connect after the gate is released.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (lock_wifi_state()) {
        s_provisioning_ready = true;
        s_wifi_mode_transitioning = false;
        unlock_wifi_state();
    }

    unlock_wifi_effect();

    rk_net_evt_cb(RK_NET_EVT_AP_STOPPED, NULL);

    // The STA_START event will trigger connect_now()
}

void wifi_mgr_set_power_save(bool enable) {
    if (!lock_wifi_effect()) {
        return;
    }
    if (!lock_wifi_state()) {
        unlock_wifi_effect();
        return;
    }
    const bool can_set_power_save = s_started && !s_ap_mode;
    unlock_wifi_state();
    if (!can_set_power_save) {
        unlock_wifi_effect();
        return;  // Only change power save in STA mode
    }

    wifi_ps_type_t ps_type = enable ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
    esp_err_t err = esp_wifi_set_ps(ps_type);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi power save %s", enable ? "enabled (modem sleep)" : "disabled");
    } else {
        ESP_LOGW(TAG, "Failed to set WiFi power save: %s", esp_err_to_name(err));
    }
    unlock_wifi_effect();
}

__attribute__((weak)) void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    (void)evt;
    (void)ip_opt;
}

const char *wifi_mgr_get_hostname(void) {
    return get_device_hostname();
}
