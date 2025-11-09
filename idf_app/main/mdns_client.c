#include "mdns_client.h"

#include <esp_err.h>
#include <esp_log.h>
#include <mdns.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mdns_client";
static const char *SERVICE_TYPE = "_roonknob";
static const char *SERVICE_PROTO = "_tcp";

static void copy_str(char *dst, size_t len, const char *src) {
    if (!dst || len == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    size_t n = strnlen(src, len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void mdns_client_init(const char *hostname) {
    esp_err_t err = mdns_init();
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns init failed: %s", esp_err_to_name(err));
        return;
    }
    const char *host = (hostname && hostname[0]) ? hostname : "roon-knob";
    mdns_hostname_set(host);
    mdns_instance_name_set("Roon Knob");

    mdns_txt_item_t txt[] = {
        {"product", "roon-knob"},
    };
    mdns_service_add(NULL, "_device-info", "_udp", 9, txt, sizeof(txt) / sizeof(txt[0]));
}

static bool txt_find_base(const mdns_result_t *result, char *out, size_t len) {
    if (!result || !result->txt || result->txt_count == 0) {
        return false;
    }
    for (size_t i = 0; i < result->txt_count; ++i) {
        const mdns_txt_item_t *item = &result->txt[i];
        if (item->key && strcmp(item->key, "base") == 0 && item->value) {
            copy_str(out, len, item->value);
            return true;
        }
    }
    return false;
}

bool mdns_client_discover_bridge(rk_cfg_t *cfg) {
    if (!cfg) {
        return false;
    }
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(SERVICE_TYPE, SERVICE_PROTO, 2000, 4, &results);
    if (err != ESP_OK || !results) {
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "mdns query failed: %s", esp_err_to_name(err));
        }
        if (results) {
            mdns_query_results_free(results);
        }
        return false;
    }

    bool updated = false;
    char url[sizeof(cfg->bridge_base)] = {0};

    for (mdns_result_t *r = results; r && !updated; r = r->next) {
        if (txt_find_base(r, url, sizeof(url))) {
            updated = true;
            break;
        }
        if (r->hostname && r->port) {
            snprintf(url, sizeof(url), "http://%s:%u", r->hostname, r->port);
            updated = true;
        }
    }

    mdns_query_results_free(results);

    if (updated && url[0] != '\0' && strcmp(cfg->bridge_base, url) != 0) {
        copy_str(cfg->bridge_base, sizeof(cfg->bridge_base), url);
        if (!rk_cfg_save(cfg)) {
            ESP_LOGW(TAG, "failed to save cfg after mdns update");
        }
        ESP_LOGI(TAG, "bridge base set to %s", cfg->bridge_base);
        return true;
    }
    return false;
}
