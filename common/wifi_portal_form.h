#pragma once

#include "wifi_manager.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    rk_wifi_scan_state_t state;
    rk_wifi_network_t networks[RK_WIFI_SCAN_MAX_NETWORKS];
    size_t count;
} rk_wifi_portal_scan_t;

/* Start or restart the shared non-blocking scan and snapshot any ready results. */
void rk_wifi_portal_scan_prepare(rk_wifi_portal_scan_t *scan, bool restart);

/* Render HTML-escaped <option> rows for the shared network selector. */
size_t rk_wifi_portal_render_options(const rk_wifi_portal_scan_t *scan,
                                     char *out, size_t capacity);

const char *rk_wifi_portal_scan_placeholder(const rk_wifi_portal_scan_t *scan);
bool rk_wifi_portal_scan_should_refresh(const rk_wifi_portal_scan_t *scan);

/* Manual input wins when present; otherwise use the selected visible SSID. */
bool rk_wifi_portal_resolve_ssid(const char *selected, const char *manual,
                                 char *out, size_t capacity);

#define RK_WIFI_PORTAL_SELECT_OPEN                                           \
    "<label>WiFi Network (SSID)</label>"                                    \
    "<select id='ssid_scan' name='ssid'><option value=''>"

#define RK_WIFI_PORTAL_SELECT_CLOSE                                          \
    "</select>"                                                            \
    "<label>Or enter a hidden network</label>"                             \
    "<input id='ssid_manual' type='text' name='ssid_manual' maxlength='32' " \
    "placeholder='Hidden WiFi name'>"

#define RK_WIFI_PORTAL_SELECT_CSS_LITERAL                                    \
    "select{width:100%;padding:10px;border:1px solid #333;border-radius:5px;" \
    "background:#0f0f1a;color:#fff;box-sizing:border-box;}"

/* Use inside an snprintf format string, where a literal percent is doubled. */
#define RK_WIFI_PORTAL_SELECT_CSS_FORMAT                                     \
    "select{width:100%%;padding:10px;border:1px solid #333;border-radius:5px;" \
    "background:#0f0f1a;color:#fff;box-sizing:border-box;}"

/* Password field with a "Show password" toggle. Self-contained (inline handler,
 * no '%'), so it is safe both as a literal and inside an snprintf format. The
 * checkbox has no name, so it is never submitted with the form. */
#define RK_WIFI_PORTAL_PASSWORD_INPUT(placeholder)                           \
    "<input type='password' id='pass' name='pass' maxlength='64' "          \
    "autocomplete='current-password' placeholder='" placeholder "'>"        \
    "<label style='display:flex;align-items:center;gap:8px;margin:6px 0 0;" \
    "font-weight:normal'><input type='checkbox' style='width:auto;margin:0' " \
    "onchange=\"document.getElementById('pass').type="                      \
    "this.checked?'text':'password'\">Show password</label>"

#define RK_WIFI_PORTAL_AUTO_REFRESH_SCRIPT                                   \
    "<script>setTimeout(function(){location.reload();},1200);</script>"

#ifdef __cplusplus
}
#endif
