#include "config_store.h"
#include "encoder_input.h"
#include "http_client.h"
#include "mdns_client.h"
#include "ui.h"
#include "ui_network.h"
#include "wifi_manager.h"

#include <esp_err.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define POLL_DELAY_MS 1000
#define MAX_LINE 128

struct now_playing {
    char line1[MAX_LINE];
    char line2[MAX_LINE];
    bool is_playing;
    int volume;
};

static rk_cfg_t s_cfg_cache;

static void copy_value(const char *data, const char *key, char *out, size_t len) {
    const char *found = strstr(data, key);
    if (!found) {
        return;
    }
    const char *start = strchr(found, '"');
    if (!start) {
        return;
    }
    start++;
    const char *end = strchr(start, '"');
    if (!end) {
        return;
    }
    size_t copy_len = (size_t)(end - start);
    if (copy_len >= len) {
        copy_len = len - 1;
    }
    memcpy(out, start, copy_len);
    out[copy_len] = '\0';
}

static bool fetch_now_playing(struct now_playing *state, const rk_cfg_t *cfg) {
    if (!cfg || !cfg->bridge_base[0]) {
        return false;
    }
    char url[256];
    snprintf(url, sizeof(url), "%s/now_playing?zone_id=%s", cfg->bridge_base, cfg->zone_id);
    char *resp = NULL;
    size_t resp_len = 0;
    if (http_get(url, &resp, &resp_len) != 0 || !resp) {
        return false;
    }

    copy_value(resp, "\"line1\"", state->line1, sizeof(state->line1));
    copy_value(resp, "\"line2\"", state->line2, sizeof(state->line2));
    state->is_playing = strstr(resp, "\"is_playing\":true") != NULL;
    const char *vol_key = strstr(resp, "\"volume\"");
    if (vol_key) {
        const char *colon = strchr(vol_key, ':');
        if (colon) {
            state->volume = atoi(colon + 1);
        }
    }
    http_free(resp);
    return true;
}

static void ensure_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static bool cfg_has_data(const rk_cfg_t *cfg) {
    return cfg && cfg->cfg_ver != 0;
}

static void refresh_cfg_cache(void) {
    rk_cfg_t cfg = {0};
    if (rk_cfg_load(&cfg) || cfg_has_data(&cfg)) {
        s_cfg_cache = cfg;
    }
}

void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    ui_network_on_event(evt, ip_opt);
    if (evt == RK_NET_EVT_GOT_IP) {
        rk_cfg_t cfg = {0};
        if ((rk_cfg_load(&cfg) || cfg_has_data(&cfg)) && mdns_client_discover_bridge(&cfg)) {
            s_cfg_cache = cfg;
        }
    }
}

void app_main(void) {
    ensure_nvs();

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char hostname[32];
    snprintf(hostname, sizeof(hostname), "roon-knob-%02X%02X", mac[4], mac[5]);
    mdns_client_init(hostname);

    ui_init();
    ui_network_register_menu();
    encoder_input_init();
    wifi_mgr_start();

    rk_cfg_t cfg = {0};
    while (!(rk_cfg_load(&cfg) || cfg_has_data(&cfg))) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    s_cfg_cache = cfg;

    struct now_playing state = {0};
    strncpy(state.line1, "Waiting for bridge", sizeof(state.line1));

    while (true) {
        refresh_cfg_cache();
        bool ok = fetch_now_playing(&state, &s_cfg_cache);
        ui_update(state.line1, state.line2, state.is_playing, state.volume);
        ui_set_status(ok);
        vTaskDelay(pdMS_TO_TICKS(POLL_DELAY_MS));
    }
}
