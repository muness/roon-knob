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
    const char *host = (hostname && hostname[0]) ? hostname : "hiphi-dial";
    ESP_LOGI(TAG, "mDNS hostname: %s", host);
    mdns_hostname_set(host);

    // Set instance name to match hostname for UniFi device discovery
    mdns_instance_name_set(host);

    // Advertise HTTP service for UniFi's device discovery protocols
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    // Also advertise device info for compatibility
    mdns_txt_item_t txt[] = {
        {"product", "hiphi-dial"},
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
        if (!found) {
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
            if (platform_mdns_build_bridge_url(url, sizeof(url), resolved_ip,
                                                r->port, txt_base)) {
                ESP_LOGI(TAG, "  Selected bridge endpoint: %s%s", url,
                         resolved_ip[0] ? " (resolved IPv4)" : " (TXT fallback)");
                found = true;
            }
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
