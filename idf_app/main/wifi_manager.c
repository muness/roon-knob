#include "wifi_manager.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <string.h>

#include "sdkconfig.h"

#include "platform/platform_storage.h"

static const char *TAG = "wifi_mgr";
static const uint32_t s_backoff_ms[] = {500, 1000, 2000, 4000, 8000, 16000, 30000};

static rk_cfg_t s_cfg;
static bool s_cfg_loaded;
static esp_netif_t *s_netif;
static esp_timer_handle_t s_retry_timer;
static size_t s_backoff_idx;
static bool s_started;
static char s_ip[16];

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
    if (cfg->bridge_base[0] == '\0' || strcmp(cfg->bridge_base, "http://127.0.0.1:8088") == 0) {
        copy_str(cfg->bridge_base, sizeof(cfg->bridge_base), CONFIG_RK_DEFAULT_BRIDGE_BASE);
        ESP_LOGI(TAG, "Applied bridge_base from config: %s", CONFIG_RK_DEFAULT_BRIDGE_BASE);
    }
}

static void apply_full_defaults(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    apply_wifi_defaults(cfg);
    copy_str(cfg->bridge_base, sizeof(cfg->bridge_base), CONFIG_RK_DEFAULT_BRIDGE_BASE);
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

static void schedule_retry(void);

static void connect_now(void) {
    if (!s_cfg_loaded) {
        ensure_cfg_loaded();
    }
    if (s_cfg.ssid[0] == '\0') {
        ESP_LOGW(TAG, "SSID empty; skipping connect");
        return;
    }
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

static void schedule_retry(void) {
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
    rk_net_evt_cb(RK_NET_EVT_FAIL, NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        reset_backoff();
        connect_now();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        schedule_retry();
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

    reset_backoff();
    rk_net_evt_cb(RK_NET_EVT_GOT_IP, s_ip);
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
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    const esp_timer_create_args_t retry_args = {
        .callback = &retry_timer_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_wifi_start());
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
    connect_now();
}

void wifi_mgr_forget_wifi(void) {
    platform_storage_reset_wifi_only(&s_cfg);
    s_cfg_loaded = false;
    ensure_cfg_loaded();
    reset_backoff();
    if (s_started) {
        connect_now();
    }
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

__attribute__((weak)) void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    (void)evt;
    (void)ip_opt;
}
