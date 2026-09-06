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
