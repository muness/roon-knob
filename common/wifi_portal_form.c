#include "wifi_portal_form.h"

#include <stdio.h>
#include <string.h>

static void html_escape(const char *src, char *dst, size_t dst_len) {
    size_t used = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; *src && used + 1 < dst_len; ++src) {
        const char *replacement = NULL;
        switch (*src) {
        case '&': replacement = "&amp;"; break;
        case '<': replacement = "&lt;"; break;
        case '>': replacement = "&gt;"; break;
        case '\"': replacement = "&quot;"; break;
        case '\'': replacement = "&#39;"; break;
        default:
            dst[used++] = *src;
            continue;
        }
        const size_t length = strlen(replacement);
        if (used + length >= dst_len) {
            break;
        }
        memcpy(dst + used, replacement, length);
        used += length;
    }
    dst[used] = '\0';
}

void rk_wifi_portal_scan_prepare(rk_wifi_portal_scan_t *scan, bool restart) {
    if (!scan) {
        return;
    }
    memset(scan, 0, sizeof(*scan));
    scan->state = wifi_mgr_scan_state();
    if (restart || scan->state == RK_WIFI_SCAN_IDLE ||
        scan->state == RK_WIFI_SCAN_FAILED) {
        (void)wifi_mgr_scan_start();
        scan->state = wifi_mgr_scan_state();
    }
    if (scan->state == RK_WIFI_SCAN_READY) {
        scan->count = wifi_mgr_scan_results_copy(
            scan->networks, RK_WIFI_SCAN_MAX_NETWORKS);
    }
}

size_t rk_wifi_portal_render_options(const rk_wifi_portal_scan_t *scan,
                                     char *out, size_t capacity) {
    if (!out || capacity == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!scan) {
        return 0;
    }
    size_t used = 0;
    for (size_t i = 0; i < scan->count && used + 1 < capacity; ++i) {
        char escaped[sizeof(scan->networks[i].ssid) * 6] = {0};
        html_escape(scan->networks[i].ssid, escaped, sizeof(escaped));
        int written = snprintf(out + used, capacity - used,
                               "<option value='%s'>%s (%d dBm)</option>",
                               escaped, escaped, (int)scan->networks[i].rssi);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= capacity - used) {
            used = capacity - 1;
            break;
        }
        used += (size_t)written;
    }
    out[used] = '\0';
    return used;
}

const char *rk_wifi_portal_scan_placeholder(const rk_wifi_portal_scan_t *scan) {
    if (!scan || scan->state == RK_WIFI_SCAN_RUNNING) {
        return "Scanning for nearby networks...";
    }
    if (scan->state == RK_WIFI_SCAN_FAILED) {
        return "Scan failed - reload to retry";
    }
    return scan->count == 0 ? "No nearby networks found"
                            : "Choose a nearby network";
}

bool rk_wifi_portal_scan_should_refresh(const rk_wifi_portal_scan_t *scan) {
    return scan && scan->state == RK_WIFI_SCAN_RUNNING;
}

bool rk_wifi_portal_resolve_ssid(const char *selected, const char *manual,
                                 char *out, size_t capacity) {
    if (!out || capacity == 0) {
        return false;
    }
    const char *chosen = manual && manual[0] ? manual : selected;
    if (!chosen || !chosen[0] || strlen(chosen) >= capacity) {
        out[0] = '\0';
        return false;
    }
    snprintf(out, capacity, "%s", chosen);
    return true;
}
