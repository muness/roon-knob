#include "wifi_manager.h"
#include "captive_portal.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <string.h>

#include "sdkconfig.h"

#include "platform/platform_storage.h"

static const char *TAG = "wifi_mgr";
static const uint32_t s_backoff_ms[] = {500, 1000, 2000, 4000, 8000, 16000, 30000};
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
#define AP_SSID "roon-knob-setup"
#define AP_MAX_CONNECTIONS 2
#define STA_FAIL_THRESHOLD 5  // Switch to AP after this many consecutive STA failures

static rk_cfg_t s_cfg;
static bool s_cfg_loaded;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_timer_handle_t s_retry_timer;
static size_t s_backoff_idx;
static bool s_started;
static char s_ip[16];
static bool s_ap_mode;           // true when in AP provisioning mode
static int s_sta_fail_count;     // consecutive STA connection failures

static void copy_str(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    size_t in_len = strnlen(src, dst_len - 1);
    memcpy(dst, src, in_len);
    dst[in_len] = '\0';
}

static bool have_blob(const rk_cfg_t *cfg) {
    return cfg && cfg->cfg_ver != 0;
}

static void apply_wifi_defaults(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    copy_str(cfg->ssid, sizeof(cfg->ssid), CONFIG_RK_DEFAULT_SSID);
    copy_str(cfg->pass, sizeof(cfg->pass), CONFIG_RK_DEFAULT_PASS);
    // Don't set bridge_base here - mDNS discovery is the primary method
}

static void apply_full_defaults(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    apply_wifi_defaults(cfg);
    // Don't set bridge_base here - mDNS discovery is the primary method
    // zone_id is left empty - user will select from available zones
    cfg->zone_id[0] = '\0';
}

static void ensure_cfg_loaded(void) {
    rk_cfg_t cfg = {0};
    bool load_ok = platform_storage_load(&cfg);
    (void)load_ok;  // Unused but kept for clarity
    bool blob_exists = have_blob(&cfg);
    bool has_wifi_creds = (cfg.ssid[0] != '\0');

    if (!blob_exists) {
        apply_full_defaults(&cfg);
        platform_storage_save(&cfg);
    } else if (!has_wifi_creds) {
        // Apply WiFi defaults when SSID is empty
        apply_wifi_defaults(&cfg);
        platform_storage_save(&cfg);
    }
    s_cfg = cfg;
    s_cfg_loaded = true;
}

static esp_err_t apply_wifi_config(void) {
    if (!s_cfg_loaded) {
        ensure_cfg_loaded();
    }
    wifi_config_t cfg = {0};
    copy_str((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), s_cfg.ssid);
    copy_str((char *)cfg.sta.password, sizeof(cfg.sta.password), s_cfg.pass);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static void reset_backoff(void) {
    s_backoff_idx = 0;
}

static void schedule_retry_with_reason(uint8_t reason);
static void schedule_retry(void);
static void start_ap_mode(void);

static void connect_now(void) {
    if (s_ap_mode) {
        return;  // Don't try STA when in AP mode
    }
    if (!s_cfg_loaded) {
        ensure_cfg_loaded();
    }
    if (s_cfg.ssid[0] == '\0') {
        ESP_LOGW(TAG, "SSID empty; starting AP mode for provisioning");
        start_ap_mode();
        return;
    }
    ESP_LOGI(TAG, "Connecting to WiFi SSID: '%s'", s_cfg.ssid);
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
    }
    if (apply_wifi_config() != ESP_OK) {
        ESP_LOGE(TAG, "failed to apply Wi-Fi config");
        schedule_retry();
        return;
    }
    rk_net_evt_cb(RK_NET_EVT_CONNECTING, NULL);
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "disconnect failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect failed: %s", esp_err_to_name(err));
        schedule_retry();
    }
}

static void retry_timer_cb(void *arg) {
    (void)arg;
    connect_now();
}

static void schedule_retry_with_reason(uint8_t reason) {
    s_sta_fail_count++;

    // Get human-readable reason and specific event type
    rk_net_evt_t evt = RK_NET_EVT_FAIL;
    s_last_error = get_disconnect_reason_str(reason, &evt);

    ESP_LOGW(TAG, "WiFi disconnected: %s (reason %d, attempt %d/%d)",
             s_last_error, reason, s_sta_fail_count, STA_FAIL_THRESHOLD);

    // Switch to AP mode after too many failures
    if (s_sta_fail_count >= STA_FAIL_THRESHOLD) {
        ESP_LOGW(TAG, "Too many STA failures, switching to AP mode for provisioning");
        start_ap_mode();
        return;
    }

    uint32_t delay = s_backoff_ms[s_backoff_idx];
    if (s_backoff_idx + 1 < (sizeof(s_backoff_ms) / sizeof(s_backoff_ms[0]))) {
        s_backoff_idx++;
    }
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
        esp_err_t err = esp_timer_start_once(s_retry_timer, delay * 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "retry timer start failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "retry timer missing; reconnect immediately");
        connect_now();
    }
    // Emit specific event for UI (e.g., RK_NET_EVT_WRONG_PASSWORD)
    rk_net_evt_cb(evt, s_last_error);
}

static void schedule_retry(void) {
    // Called when we don't have a specific reason (e.g., config apply failed)
    schedule_retry_with_reason(0);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        reset_backoff();
        s_last_error = NULL;  // Clear last error on new connection attempt
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
    esp_ip4addr_ntoa(&ip, s_ip, sizeof(s_ip));

    ESP_LOGI(TAG, "Connected to WiFi SSID: '%s', IP: %s", s_cfg.ssid, s_ip);
    reset_backoff();
    s_sta_fail_count = 0;  // Reset failure count on successful connection
    s_last_error = NULL;   // Clear last error on success
    rk_net_evt_cb(RK_NET_EVT_GOT_IP, s_ip);
}

static void start_ap_mode(void) {
    if (s_ap_mode) {
        return;  // Already in AP mode
    }

    ESP_LOGI(TAG, "Starting AP mode for provisioning (SSID: %s)", AP_SSID);

    // Stop STA mode
    esp_wifi_stop();

    // Create AP netif if needed
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    // Configure AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = "",  // Open network for easy provisioning
            .max_connection = AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_mode = true;
    s_sta_fail_count = 0;

    // Start captive portal HTTP server
    captive_portal_start();

    // Notify UI that we're in AP mode (IP is always 192.168.4.1 for AP)
    rk_net_evt_cb(RK_NET_EVT_AP_STARTED, "192.168.4.1");
}

void wifi_mgr_start(void) {
    if (s_started) {
        return;
    }
    s_started = true;

    ensure_cfg_loaded();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        return;
    }
    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
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
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_wifi_start());

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
}

void wifi_mgr_reconnect(const rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    if (!s_started) {
        ESP_LOGW(TAG, "wifi_mgr_reconnect before start");
        if (!platform_storage_save(cfg)) {
            ESP_LOGW(TAG, "failed to persist cfg");
        }
        s_cfg = *cfg;
        s_cfg_loaded = true;
        return;
    }
    s_cfg = *cfg;
    s_cfg_loaded = true;
    if (!platform_storage_save(&s_cfg)) {
        ESP_LOGW(TAG, "failed to persist cfg");
    }
    reset_backoff();
    s_sta_fail_count = 0;  // Reset failure count for new credentials

    // If in AP mode, stop it first before connecting
    if (s_ap_mode) {
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
    if (s_started) {
        esp_wifi_stop();
    }

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
    if (s_ip[0] == '\0') {
        buf[0] = '\0';
        return false;
    }
    copy_str(buf, n, s_ip);
    return true;
}

void wifi_mgr_get_ssid(char *buf, size_t n) {
    if (!buf || n == 0) {
        return;
    }
    if (!s_cfg_loaded) {
        ensure_cfg_loaded();
    }
    copy_str(buf, n, s_cfg.ssid);
}

bool wifi_mgr_is_ap_mode(void) {
    return s_ap_mode;
}

const char *wifi_mgr_get_last_error(void) {
    return s_last_error;
}

int wifi_mgr_get_retry_count(void) {
    return s_sta_fail_count;
}

int wifi_mgr_get_retry_max(void) {
    return STA_FAIL_THRESHOLD;
}

void wifi_mgr_stop(void) {
    if (!s_started) {
        return;
    }

    ESP_LOGI(TAG, "Stopping WiFi completely (for BLE mode)");

    // Stop retry timer
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
    }

    // Stop captive portal if running
    captive_portal_stop();

    // Stop WiFi
    esp_wifi_stop();
    esp_wifi_deinit();

    // Deinit netif and event loop - but keep them as they're shared
    // Just mark as stopped so we can restart later
    s_started = false;
    s_ap_mode = false;
    s_sta_fail_count = 0;
    s_ip[0] = '\0';

    ESP_LOGI(TAG, "WiFi stopped");
}

void wifi_mgr_stop_ap(void) {
    if (!s_ap_mode) {
        return;
    }

    ESP_LOGI(TAG, "Stopping AP mode, switching to STA");

    // Stop captive portal first
    captive_portal_stop();

    // Stop AP
    esp_wifi_stop();

    // Switch to STA mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_mode = false;
    s_sta_fail_count = 0;
    s_ip[0] = '\0';

    rk_net_evt_cb(RK_NET_EVT_AP_STOPPED, NULL);

    // The STA_START event will trigger connect_now()
}

__attribute__((weak)) void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    (void)evt;
    (void)ip_opt;
}
