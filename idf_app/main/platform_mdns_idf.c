#include "platform/platform_mdns.h"

#include <esp_err.h>
#include <esp_log.h>
#include <mdns.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "platform_mdns";
static const char *SERVICE_TYPE = "_roonknob";
static const char *SERVICE_PROTO = "_tcp";

static void copy_str(char *dst, size_t len, const char *src) {
    if (!dst || len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void platform_mdns_init(const char *hostname) {
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
    if (!result || !out || len == 0) {
        return false;
    }
    if (!result->txt) {
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

bool platform_mdns_discover_base_url(char *out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    ESP_LOGI(TAG, "Querying mDNS for %s.%s...", SERVICE_TYPE, SERVICE_PROTO);
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(SERVICE_TYPE, SERVICE_PROTO, 3000, 4, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!results) {
        ESP_LOGW(TAG, "mDNS query returned no results");
        return false;
    }
    bool found = false;
    char url[128] = {0};
    int count = 0;
    for (mdns_result_t *r = results; r; r = r->next) {
        count++;
        ESP_LOGI(TAG, "mDNS result %d: hostname=%s port=%d txt_count=%zu",
                 count, r->hostname ? r->hostname : "(null)", r->port, r->txt_count);
        if (!found && txt_find_base(r, url, sizeof(url))) {
            ESP_LOGI(TAG, "  Found base TXT: %s", url);
            found = true;
        }
        // Prefer IP address over hostname - ESP32 lwIP has issues resolving .local hostnames
        if (!found && r->addr && r->port) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));
            snprintf(url, sizeof(url), "http://%s:%u", ip_str, r->port);
            ESP_LOGI(TAG, "  Using IP:port: %s (hostname=%s)", url, r->hostname ? r->hostname : "(null)");
            found = true;
        }
    }
    ESP_LOGI(TAG, "mDNS: found %d results, selected: %s", count, found ? url : "(none)");
    mdns_query_results_free(results);
    if (found && url[0]) {
        copy_str(out, len, url);
    }
    return found && out[0];
}

bool platform_mdns_resolve_local(const char *hostname, char *ip_out, size_t ip_len) {
    if (!hostname || !ip_out || ip_len < 16) {
        return false;
    }
    ip_out[0] = '\0';

    // Strip .local suffix if present
    char host[64];
    copy_str(host, sizeof(host), hostname);
    char *suffix = strstr(host, ".local");
    if (suffix) {
        *suffix = '\0';
    }

    ESP_LOGI(TAG, "Resolving mDNS hostname: %s", host);
    esp_ip4_addr_t addr;
    addr.addr = 0;
    esp_err_t err = mdns_query_a(host, 2000, &addr);
    if (err != ESP_OK || addr.addr == 0) {
        ESP_LOGW(TAG, "mDNS resolve failed for %s: %s", host, esp_err_to_name(err));
        return false;
    }

    snprintf(ip_out, ip_len, IPSTR, IP2STR(&addr));
    ESP_LOGI(TAG, "Resolved %s -> %s", host, ip_out);
    return true;
}
