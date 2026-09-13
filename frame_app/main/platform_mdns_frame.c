// platform_mdns.h implementation — identical to idf_app/platform_mdns_idf.c
// Only change: device-info product name is "hiphi-frame"

#include "platform/platform_mdns.h"
#include "platform/platform_mdns_endpoint.h"

#include <esp_err.h>
#include <esp_log.h>
#include <mdns.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>
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

void platform_mdns_observe_bridge(const char *selected, platform_mdns_observation_t *out) {
    memset(out, 0, sizeof(*out));
    mdns_result_t *results = NULL;
    if (mdns_query_ptr(SERVICE_TYPE, SERVICE_PROTO, 3000, 16, &results) != ESP_OK) return;
    for (mdns_result_t *r = results; r; r = r->next) {
        if (!r->hostname || !r->port) continue;
        char identity[128], endpoint[128] = {0};
        int n = snprintf(identity, sizeof(identity), "http://%s%s:%u", r->hostname,
                         strstr(r->hostname, ".local") ? "" : ".local", (unsigned)r->port);
        if (n <= 0 || (size_t)n >= sizeof(identity)) continue;
        bool address = false;
        for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
            if (a->addr.type != ESP_IPADDR_TYPE_V4 || !a->addr.u_addr.ip4.addr) continue;
            snprintf(endpoint, sizeof(endpoint), "http://" IPSTR ":%u",
                     IP2STR(&a->addr.u_addr.ip4), (unsigned)r->port);
            platform_mdns_consider_record(out, selected, identity, endpoint);
            address = true;
        }
        if (!address) platform_mdns_consider_record(out, selected, identity, "");
    }
    mdns_query_results_free(results);
}
bool platform_mdns_resolve_base_url(const char *base, char *out, size_t len,
                                    bool *used_dns, bool *mdns_failed) {
    char host[64], ip[16] = {0}; const char *suffix;
    out[0] = 0; *used_dns = false; *mdns_failed = false;
    if (!platform_mdns_url_host(base, host, sizeof(host), &suffix)) return false;
    struct in_addr literal;
    if (inet_pton(AF_INET, host, &literal) == 1) inet_ntop(AF_INET, &literal, ip, sizeof(ip));
    else if (!platform_mdns_resolve_local(host, ip, sizeof(ip))) {
        *mdns_failed = true;
        struct addrinfo hints = {0}, *answer = NULL;
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host, NULL, &hints, &answer) != 0 || !answer) return false;
        struct sockaddr_in *addr = (struct sockaddr_in *)answer->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        freeaddrinfo(answer);
        *used_dns = true;
    }
    int n = snprintf(out, len, "http://%s%s", ip, suffix);
    if (n <= 0 || (size_t)n >= len) { out[0] = 0; return false; }
    return true;
}
