#include "platform_mdns_endpoint.h"

#include <stdio.h>
#include <string.h>

bool platform_mdns_build_bridge_url(char *out, size_t len,
                                    const char *resolved_ipv4,
                                    uint16_t port,
                                    const char *txt_base) {
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';

    if (resolved_ipv4 && resolved_ipv4[0] && port > 0) {
        int written = snprintf(out, len, "http://%s:%u", resolved_ipv4,
                               (unsigned)port);
        if (written > 0 && (size_t)written < len) {
            return true;
        }
        out[0] = '\0';
        return false;
    }

    if (txt_base && txt_base[0]) {
        size_t copy_len = strlen(txt_base);
        if (copy_len >= len) {
            return false;
        }
        memcpy(out, txt_base, copy_len + 1);
        return true;
    }

    return false;
}

bool platform_mdns_consider_bridge_url(char *selected, size_t selected_len,
                                       char *txt_fallback, size_t fallback_len,
                                       const char *resolved_ipv4,
                                       uint16_t port,
                                       const char *txt_base) {
    if (!selected || selected_len == 0 || !txt_fallback || fallback_len == 0) {
        return false;
    }

    char candidate[128] = {0};
    if (!platform_mdns_build_bridge_url(candidate, sizeof(candidate),
                                        resolved_ipv4, port, txt_base)) {
        return false;
    }

    size_t candidate_len = strlen(candidate);
    if (resolved_ipv4 && resolved_ipv4[0] && port > 0) {
        if (candidate_len >= selected_len) {
            return false;
        }
        memcpy(selected, candidate, candidate_len + 1);
        return true;
    }

    if (txt_fallback[0] == '\0' && candidate_len < fallback_len) {
        memcpy(txt_fallback, candidate, candidate_len + 1);
    }
    return false;
}

/* Existing IPs have no persisted service identity. A broad PTR query cannot
 * safely distinguish the selected bridge from a different installation. */
bool platform_mdns_select_bridge_update(
    const char *current, bool from_mdns, char *out, size_t len,
    platform_mdns_discover_fn discover, platform_mdns_resolve_fn resolve,
    platform_mdns_verify_fn verify) {
    if (!out || !len) return false;
    out[0] = '\0';
    if (!current) return false;
    if (!current[0]) return discover && discover(out, len);
    if (!from_mdns || !resolve || !verify) return false;

    /* Migrate only the saved HTTP hostname, preserving its port/path. Do not
     * change schemes or reinterpret userinfo/IPv6 as a local hostname. */
    if (strncmp(current, "http://", 7) != 0) return false;
    const char *host = current + 7;
    size_t host_len = strcspn(host, ":/?#");
    if (!host_len || host_len >= 64) return false;
    bool has_letter = false;
    for (size_t i = 0; i < host_len; ++i) {
        unsigned char ch = (unsigned char)host[i];
        bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        if (!letter && !(ch >= '0' && ch <= '9') && ch != '-' && ch != '.')
            return false;
        has_letter = has_letter || letter;
    }
    if (!has_letter) return false; /* keep saved numeric addresses */
    char hostname[64];
    memcpy(hostname, host, host_len);
    hostname[host_len] = '\0';
    char ip[16] = {0};
    if (!resolve(hostname, ip, sizeof(ip)) || !ip[0]) return false;
    char candidate[128];
    int written = snprintf(candidate, sizeof(candidate), "http://%s%s",
                           ip, host + host_len);
    if (written <= 0 || (size_t)written >= sizeof(candidate) ||
        (size_t)written >= len || !verify(candidate)) return false;
    memcpy(out, candidate, (size_t)written + 1);
    return true;
}
