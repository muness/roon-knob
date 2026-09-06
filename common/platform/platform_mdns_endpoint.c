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
