// platform_mdns.h implementation — identical to idf_app/platform_mdns_idf.c
// Only change: device-info product name is "hiphi-frame"

#include "platform/platform_mdns.h"
#include "platform/platform_mdns_endpoint.h"

#include <esp_err.h>
#include <esp_log.h>
#include <mdns.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "platform_mdns";
static const char *SERVICE_TYPE = "_roonknob";
static const char *SERVICE_PROTO = "_tcp";
#ifndef PLATFORM_MDNS_PRODUCT
#define PLATFORM_MDNS_PRODUCT "hiphi-frame"
#endif
static volatile bool s_mdns_ready = false;

static void copy_str(char *dst, size_t len, const char *src) {
    if (!dst || len == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strnlen(src, len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void platform_mdns_init(const char *hostname) {
    esp_err_t err = mdns_init();
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns init failed: %s", esp_err_to_name(err));
        return;
    }
    const char *host = (hostname && hostname[0]) ? hostname : PLATFORM_MDNS_PRODUCT;
    ESP_LOGI(TAG, "mDNS hostname: %s", host);
    mdns_hostname_set(host);
    mdns_instance_name_set(host);

    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    mdns_txt_item_t txt[] = {
        {"product", PLATFORM_MDNS_PRODUCT},
    };
    mdns_service_add(NULL, "_device-info", "_udp", 9, txt, sizeof(txt) / sizeof(txt[0]));
    s_mdns_ready = true;
    ESP_LOGI(TAG, "mDNS ready");
}

bool platform_mdns_is_ready(void) { return s_mdns_ready; }

static bool txt_find_base(const mdns_result_t *result, char *out, size_t len) {
    if (!result || !out || len == 0 || !result->txt) return false;
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
    if (!out || len == 0) return false;
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
    char txt_fallback[128] = {0};
    for (mdns_result_t *r = results; r; r = r->next) {
        char resolved_ip[16] = {0};
        for (mdns_ip_addr_t *addr = r->addr; addr; addr = addr->next) {
            if (addr->addr.type == ESP_IPADDR_TYPE_V4) {
                snprintf(resolved_ip, sizeof(resolved_ip), IPSTR,
                         IP2STR(&addr->addr.u_addr.ip4));
                break;
            }
        }
        if (!resolved_ip[0] && r->hostname) {
            platform_mdns_resolve_local(r->hostname, resolved_ip,
                                         sizeof(resolved_ip));
        }

        char txt_base[128] = {0};
        txt_find_base(r, txt_base, sizeof(txt_base));
        if (platform_mdns_consider_bridge_url(
                url, sizeof(url), txt_fallback, sizeof(txt_fallback),
                resolved_ip, r->port, txt_base)) {
            ESP_LOGI(TAG, "Selected bridge endpoint: %s (resolved IPv4)", url);
            found = true;
            break;
        }
    }
    if (!found && txt_fallback[0]) {
        copy_str(url, sizeof(url), txt_fallback);
        ESP_LOGI(TAG, "Selected bridge endpoint: %s (TXT fallback)", url);
        found = true;
    }
    mdns_query_results_free(results);
    if (found && url[0]) {
        copy_str(out, len, url);
    }
    return found && out[0];
}

bool platform_mdns_resolve_local(const char *hostname, char *ip_out, size_t ip_len) {
    if (!hostname || !ip_out || ip_len < 16) return false;
    ip_out[0] = '\0';

    char host[64];
    copy_str(host, sizeof(host), hostname);
    size_t host_len = strlen(host);
    if (host_len >= 6 && strcmp(host + host_len - 6, ".local") == 0) {
        host[host_len - 6] = '\0';
    }

    ESP_LOGI(TAG, "Resolving mDNS hostname: %s", host);
    esp_ip4_addr_t addr;
    addr.addr = 0;
    esp_err_t err = mdns_query_a(host, 2000, &addr);
    if (err != ESP_OK || addr.addr == 0) {
        ESP_LOGW(TAG, "mDNS resolve failed for %s", host);
        return false;
    }

    snprintf(ip_out, ip_len, IPSTR, IP2STR(&addr));
    ESP_LOGI(TAG, "Resolved %s -> %s", host, ip_out);
    return true;
}
