// HTTP config server - runs when connected to WiFi for remote configuration
// Access at http://<knob-ip>/ to set bridge URL

#include "config_server.h"
#include "controller_config.h"
#include "http_server_lifecycle.h"
#include "platform/platform_mdns.h"
#include "bridge_client.h"
#include "rk_ble_hid_host.h"
#include "wifi_manager.h"

#include <stdlib.h>
#include <string.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "config_server";

static httpd_handle_t s_server = NULL;

static esp_err_t send_unverified_settings(httpd_req_t *req) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req,
        "Settings saved but could not be verified. No restart was performed; please try again.");
    return ESP_FAIL;
}

static void apply_committed_wifi(bool reconnect) {
    controller_config_wifi_snapshot_t wifi = {0};
    if (controller_config_wifi_snapshot(&wifi)) {
        wifi_mgr_apply_wifi(&wifi, reconnect);
    }
}

static esp_err_t send_conflict(httpd_req_t *req, const char *message) {
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req,
                       message ? message : "Request conflicts with BLE state");
    return ESP_FAIL;
}

// HTML page for config
// Format args: current_bridge, status_class, status_text, wifi_html, bridge_value
static const char *HTML_CONFIG =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HiPhi Dial Config</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;}"
    "h1{color:#4fc3f7;margin-bottom:5px;}"
    "h2{color:#aaa;font-size:16px;margin-top:20px;}"
    ".info{color:#888;margin:10px 0;}"
    "form{background:#16213e;padding:20px;border-radius:10px;max-width:400px;}"
    "label{display:block;margin:15px 0 5px;color:#aaa;}"
    "input[type=text],input[type=url],input[type=password]{width:100%%;padding:10px;border:1px solid #333;border-radius:5px;background:#0f0f1a;color:#fff;box-sizing:border-box;}"
    "input[type=submit]{padding:12px 24px;margin-top:20px;background:#4fc3f7;color:#000;border:none;border-radius:5px;font-weight:bold;cursor:pointer;}"
    "input[type=submit]:hover{background:#29b6f6;}"
    ".btn-clear{background:#ff7043;}"
    ".btn-clear:hover{background:#ff5722;}"
    ".btn-sm{padding:6px 12px;margin:0 0 0 10px;font-size:12px;}"
    ".current{background:#0f0f1a;padding:10px;border-radius:5px;margin:10px 0;font-family:monospace;}"
    ".status{padding:10px;border-radius:5px;margin:10px 0;}"
    ".status-ok{background:#1b5e20;}"
    ".status-warn{background:#e65100;}"
    ".status-err{background:#b71c1c;}"
    ".hint{font-size:12px;color:#666;margin-top:4px;}"
    ".success{background:#2e7d32;padding:15px;border-radius:5px;margin:15px 0;}"
    ".wifi-entry{background:#0f0f1a;padding:8px 12px;border-radius:5px;margin:4px 0;display:flex;justify-content:space-between;align-items:center;max-width:400px;}"
    ".section{max-width:400px;}"
    "a{color:#4fc3f7;}"
    ".device{background:#0f0f1a;padding:10px;border-radius:5px;margin:8px 0;display:flex;justify-content:space-between;align-items:center;}"
    "</style></head><body>"
    "<h1>HiPhi Dial</h1>"
    "<p class='info'>Configure your HiPhi Dial settings</p>"
    "<p><a href='/ble'>BLE Media Remote settings</a></p>"
    "<div class='current'>"
    "<strong>Current Unified Hi-Fi Control:</strong> %s"
    "</div>"
    "<div class='status %s'>"
    "<strong>Status:</strong> %s"
    "</div>"
        "<h2>Saved WiFi Networks</h2>"
        "<div class='section'>%s</div>"
        "<p class='hint'>Saved-network changes take effect after restart.</p>"
    "<form method='POST' action='/wifi-add'>"
    "<h2>Add WiFi Network</h2>"
    "<label>SSID</label>"
    "<input type='text' name='ssid' maxlength='32' placeholder='Network name' required>"
    "<label>Password</label>"
        "<input type='password' name='pass' maxlength='64' placeholder='Password (optional)'>"
        "<p class='hint'>Up to two networks. Remove one before replacing it.</p>"
    "<input type='submit' value='Add Network'>"
    "</form>"
    "<form method='POST' action='/config'>"
    "<h2>Unified Hi-Fi Control Override</h2>"
    "<label>Unified Hi-Fi Control URL</label>"
    "<input type='url' name='bridge' maxlength='128' placeholder='http://192.168.1.x:8088' value='%s'>"
    "<p class='hint'>Leave empty for mDNS auto-discovery. Check the HiPhi Dial display for connection progress.</p>"
    "<input type='submit' value='Save'>"
    "<input type='submit' name='action' value='Clear' class='btn-clear' formnovalidate>"
    "</form></body></html>";

static const char *HTML_SUCCESS =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Saved</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;text-align:center;}"
    "h1{color:#4fc3f7;}"
    ".success{background:#2e7d32;padding:20px;border-radius:10px;max-width:300px;margin:20px auto;}"
    ".info{background:#16213e;padding:15px;border-radius:10px;max-width:300px;margin:20px auto;}"
    "</style></head><body>"
    "<h1>HiPhi Dial</h1>"
    "<div class='success'>%s</div>"
    "<div class='info'>Device will reboot automatically to apply changes...</div>"
    "</body></html>";

// URL decode a string in place
static void url_decode(char *str) {
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Parse form data to extract a field value
static bool get_form_field(const char *data, const char *field, char *out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);

    const char *start = data;
    while ((start = strstr(start, search)) != NULL) {
        if (start == data || *(start - 1) == '&') {
            break;
        }
        start++;
    }
    if (!start) {
        return false;
    }
    start += strlen(search);

    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    // URL-encoded data can be up to 3x the decoded length (e.g. ! -> %21).
    // Decode in a temporary buffer first, then truncate to fit the output.
    char encoded[256];
    if (len >= sizeof(encoded)) {
        len = sizeof(encoded) - 1;
    }

    memcpy(encoded, start, len);
    encoded[len] = '\0';
    url_decode(encoded);

    size_t decoded_len = strlen(encoded);
    if (decoded_len >= out_len) {
        decoded_len = out_len - 1;
    }
    memcpy(out, encoded, decoded_len);
    out[decoded_len] = '\0';
    return true;
}

static void html_escape(const char *src, char *dst, size_t dst_len) {
    size_t pos = 0;
    for (size_t i = 0; src && src[i] && pos + 1 < dst_len; i++) {
        const char *escaped = NULL;
        switch (src[i]) {
            case '&': escaped = "&amp;"; break;
            case '<': escaped = "&lt;"; break;
            case '>': escaped = "&gt;"; break;
            case '"': escaped = "&quot;"; break;
            case '\'': escaped = "&#39;"; break;
            default: break;
        }
        if (escaped) {
            size_t len = strlen(escaped);
            if (pos + len >= dst_len) break;
            memcpy(dst + pos, escaped, len);
            pos += len;
        } else {
            dst[pos++] = src[i];
        }
    }
    dst[pos] = '\0';
}

// Resolve .local hostname in URL to IP address via mDNS
// Modifies url in place if resolution succeeds
static void resolve_local_in_url(char *url, size_t url_len) {
    if (!url || !url[0]) return;

    // Check if URL contains .local
    char *local_pos = strstr(url, ".local");
    if (!local_pos) return;

    // Make sure it's actually the hostname suffix (followed by : or / or end)
    char after = local_pos[6];
    if (after != ':' && after != '/' && after != '\0') return;

    // Extract hostname: skip http://
    const char *host_start = strstr(url, "://");
    if (!host_start) return;
    host_start += 3;

    // Find end of hostname
    const char *host_end = host_start;
    while (*host_end && *host_end != ':' && *host_end != '/') host_end++;

    // Extract hostname
    size_t host_len = host_end - host_start;
    if (host_len == 0 || host_len >= 64) return;

    char hostname[64];
    memcpy(hostname, host_start, host_len);
    hostname[host_len] = '\0';

    // Resolve via mDNS
    char ip[16];
    if (!platform_mdns_resolve_local(hostname, ip, sizeof(ip))) {
        ESP_LOGW(TAG, "Could not resolve %s via mDNS", hostname);
        return;
    }

    // Build new URL with IP instead of hostname
    char new_url[128];
    size_t scheme_len = host_start - url;
    snprintf(new_url, sizeof(new_url), "%.*s%s%s", (int)scheme_len, url, ip, host_end);

    // Copy back if it fits
    if (strlen(new_url) < url_len) {
        strcpy(url, new_url);
        ESP_LOGI(TAG, "Resolved .local URL to: %s", url);
    }
}

// Handler for GET / - serve the config form
static esp_err_t config_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving config page");

    controller_config_snapshot_t snapshot = {0};
    if (!controller_config_snapshot(&snapshot)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings are unavailable");
        return ESP_FAIL;
    }
    const rk_cfg_t *cfg = &snapshot.value;

    const char *current = cfg->bridge_base[0] ? cfg->bridge_base : "(mDNS auto-discovery)";

    // Get bridge connection status
    const char *status_class;
    char status_text[64];
    bool bridge_connected = bridge_client_is_bridge_connected();
    int retry_count = bridge_client_get_bridge_retry_count();
    int retry_max = bridge_client_get_bridge_retry_max();

    if (bridge_connected) {
        status_class = "status-ok";
        snprintf(status_text, sizeof(status_text), "Connected");
    } else if (!cfg->bridge_base[0]) {
        status_class = "status-warn";
        snprintf(status_text, sizeof(status_text), "Searching via mDNS...");
    } else if (retry_count >= retry_max) {
        status_class = "status-err";
        snprintf(status_text, sizeof(status_text),
                 "Unreachable - check Unified Hi-Fi Control");
    } else if (retry_count > 0) {
        status_class = "status-warn";
        snprintf(status_text, sizeof(status_text), "Connecting... (%d/%d)", retry_count, retry_max);
    } else {
        status_class = "status-warn";
        snprintf(status_text, sizeof(status_text), "Connecting...");
    }

    char wifi_html[1024] = "";
    size_t wifi_pos = 0;
    for (int i = 0; i < cfg->wifi_count && i < RK_MAX_WIFI; i++) {
        char escaped_ssid[192];
        html_escape(cfg->wifi[i].ssid, escaped_ssid, sizeof(escaped_ssid));
        int written = snprintf(
            wifi_html + wifi_pos, sizeof(wifi_html) - wifi_pos,
            "<div class='wifi-entry'><span>%d. %s</span>"
            "<form method='POST' action='/wifi-remove' style='display:inline;margin:0;padding:0;'>"
            "<input type='hidden' name='idx' value='%d'>"
            "<input type='submit' value='Remove' class='btn-sm btn-clear'>"
            "</form></div>",
            i + 1, escaped_ssid, i);
        if (written < 0 || (size_t)written >= sizeof(wifi_html) - wifi_pos) {
            break;
        }
        wifi_pos += (size_t)written;
    }
    if (wifi_pos == 0) {
        snprintf(wifi_html, sizeof(wifi_html),
                 "<div class='wifi-entry'><em>No saved networks</em></div>");
    }

    // Build HTML with current values, saved networks, and bridge status.
    char *html = heap_caps_malloc(4096,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    snprintf(html, 4096, HTML_CONFIG, current, status_class, status_text,
             wifi_html, cfg->bridge_base);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    free(html);
    return ESP_OK;
}

// Handler for POST /config - save settings
static esp_err_t config_post_handler(httpd_req_t *req) {
    char buf[256] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);

    if (received <= 0) {
        ESP_LOGE(TAG, "Failed to receive POST data");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    ESP_LOGI(TAG, "Received config: %s", buf);

    // Check if Clear button was pressed
    char action[16] = {0};
    get_form_field(buf, "action", action, sizeof(action));

    const char *message;
    char bridge_base[129] = {0};
    if (strcmp(action, "Clear") == 0) {
        message = "Unified Hi-Fi Control cleared! Will use mDNS.";
        ESP_LOGI(TAG, "Bridge URL cleared");
    } else {
        char bridge[129] = {0};
        get_form_field(buf, "bridge", bridge, sizeof(bridge));

        // Validate bridge URL format if provided
        if (bridge[0] && strncmp(bridge, "http://", 7) != 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                "Invalid URL. Must start with http://");
            return ESP_FAIL;
        }

        rk_strlcpy(bridge_base, bridge, sizeof(bridge_base));

        // Resolve .local hostnames to IPs (ESP32 lwIP has issues with .local DNS)
        if (bridge[0]) {
            resolve_local_in_url(bridge_base, sizeof(bridge_base));
        }

        message = bridge_base[0] ? "Unified Hi-Fi Control URL saved!"
                                 : "Unified Hi-Fi Control cleared! Will use mDNS.";
        ESP_LOGI(TAG, "Bridge URL set to: %s", bridge_base[0] ? bridge_base : "(mDNS)");
    }

    controller_config_write_result_t result =
        controller_config_set_endpoint(bridge_base, false, NULL);
    if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
        ESP_LOGE(TAG, "Failed to save config");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save");
        return ESP_FAIL;
    }
    if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
        return send_unverified_settings(req);
    }

    // Send success response
    char *html = heap_caps_malloc(1024,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    snprintf(html, 1024, HTML_SUCCESS, message);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    free(html);

    // Reboot to apply new config
    ESP_LOGI(TAG, "Config saved, rebooting in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

static esp_err_t wifi_add_handler(httpd_req_t *req) {
    char buf[384] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};
    if (!get_form_field(buf, "ssid", ssid, sizeof(ssid)) || !ssid[0]) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }
    get_form_field(buf, "pass", pass, sizeof(pass));

    controller_config_wifi_snapshot_t wifi = {0};
    if (!controller_config_wifi_snapshot(&wifi)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings are unavailable");
        return ESP_FAIL;
    }
    bool updating_saved_network = false;
    for (size_t i = 0; i < wifi.count && i < RK_MAX_WIFI; ++i) {
        if (strcmp(wifi.entries[i].ssid, ssid) == 0) {
            updating_saved_network = true;
            break;
        }
    }
    if (wifi.count >= RK_MAX_WIFI && !updating_saved_network) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req,
                          "Two networks are already saved; remove one first");
        return ESP_FAIL;
    }
    controller_config_write_result_t result =
        controller_config_upsert_wifi(ssid, pass, false, NULL);
    if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save");
        return ESP_FAIL;
    }
    if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
        apply_committed_wifi(false);
        return send_unverified_settings(req);
    }
    apply_committed_wifi(false);

    ESP_LOGI(TAG, "Added WiFi '%s' to this Dial", ssid);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "Redirecting...");
    return ESP_OK;
}

static esp_err_t wifi_remove_handler(httpd_req_t *req) {
    char buf[64] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char idx_text[8] = {0};
    if (!get_form_field(buf, "idx", idx_text, sizeof(idx_text))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing index");
        return ESP_FAIL;
    }
    char *end = NULL;
    long parsed = strtol(idx_text, &end, 10);
    if (end == idx_text || *end != '\0' || parsed < 0 || parsed >= RK_MAX_WIFI) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
        return ESP_FAIL;
    }

    int idx = (int)parsed;
    controller_config_snapshot_t snapshot = {0};
    if (!controller_config_snapshot(&snapshot)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings are unavailable");
        return ESP_FAIL;
    }
    if (idx < snapshot.value.wifi_count) {
        ESP_LOGI(TAG, "Removing WiFi '%s' from this Dial",
                 snapshot.value.wifi[idx].ssid);
        controller_config_write_result_t result =
            controller_config_remove_wifi((size_t)idx, NULL);
        if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Failed to save");
            return ESP_FAIL;
        }
        if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
            apply_committed_wifi(false);
            return send_unverified_settings(req);
        }
        apply_committed_wifi(false);
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "Redirecting...");
    return ESP_OK;
}

static esp_err_t redirect_to_ble(httpd_req_t *req) {
    httpd_resp_set_status(req, "303 See Other");
    /* Force one follow-up refresh even if the owner task has not consumed the
     * command before the browser follows this redirect. */
    httpd_resp_set_hdr(req, "Location", "/ble?watch=1");
    httpd_resp_sendstr(req, "Redirecting...");
    return ESP_OK;
}

static bool ble_watch_requested(httpd_req_t *req) {
    char query[32];
    char watch[4];
    return httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
           httpd_query_key_value(query, "watch", watch, sizeof(watch)) == ESP_OK &&
           watch[0] == '1';
}

static bool ble_state_auto_updates(rk_ble_hid_host_state_t state) {
    return state == RK_BLE_HID_HOST_STATE_STARTING ||
           state == RK_BLE_HID_HOST_STATE_SCANNING ||
           state == RK_BLE_HID_HOST_STATE_CONNECTING ||
           state == RK_BLE_HID_HOST_STATE_STOPPING;
}

static esp_err_t ble_get_handler(httpd_req_t *req) {
    rk_ble_hid_host_status_t status = {0};
    if (rk_ble_hid_host_status_copy(&status) != RK_BLE_HID_HOST_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "BLE media remote is unavailable");
        return ESP_FAIL;
    }

    rk_ble_hid_host_device_t results[RK_BLE_HID_HOST_MAX_RESULTS];
    uint32_t scan_generation = 0;
    size_t result_count = rk_ble_hid_host_scan_results_copy(
        results, RK_BLE_HID_HOST_MAX_RESULTS, &scan_generation);

    const size_t html_size = 12288;
    char *html = heap_caps_malloc(html_size,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Out of memory");
        return ESP_FAIL;
    }

    const char *device_name =
        status.active_name[0] ? status.active_name : status.bonded_name;
    char escaped_name[RK_BLE_HID_HOST_NAME_MAX_LEN * 2] = "";
    html_escape(device_name, escaped_name, sizeof(escaped_name));

    const char *state_class = "idle";
    const char *state_title = "Ready to pair";
    const char *state_detail = "No remote is paired yet.";
    if (!status.enabled || status.state == RK_BLE_HID_HOST_STATE_DISABLED) {
        state_title = "BLE is off";
        state_detail = "Turn it on when you want to connect a media remote.";
    } else if (status.state == RK_BLE_HID_HOST_STATE_ERROR) {
        state_class = "error";
        state_title = "Bluetooth needs attention";
        state_detail = "Restart the Dial, then try again.";
    } else if (status.connected) {
        state_class = "connected";
        state_title = "Connected";
        state_detail = escaped_name[0] ? escaped_name : "Paired media remote";
    } else if (status.state == RK_BLE_HID_HOST_STATE_CONNECTING ||
               status.bonded) {
        state_class = "working";
        state_title = "Reconnecting";
        state_detail = escaped_name[0] ? escaped_name : "Paired media remote";
    } else if (status.state == RK_BLE_HID_HOST_STATE_SCANNING) {
        state_class = "working";
        state_title = "Looking for remotes";
        state_detail = "Keep your remote in pairing mode. This takes about five seconds.";
    } else if (status.state == RK_BLE_HID_HOST_STATE_STARTING) {
        state_class = "working";
        state_title = "Starting Bluetooth";
        state_detail = "The remote service will be ready shortly.";
    } else if (status.state == RK_BLE_HID_HOST_STATE_STOPPING) {
        state_class = "working";
        state_title = "Turning off Bluetooth";
        state_detail = "The remote service is shutting down.";
    }
    const bool auto_updates = ble_watch_requested(req) ||
                              ble_state_auto_updates(status.state);

    int pos = snprintf(
        html, html_size,
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>BLE Media Remote - HiPhi Dial</title>"
        "<style>"
        "*{box-sizing:border-box}body{font-family:sans-serif;margin:0;padding:24px;"
        "background:#1a1a2e;color:#eee;line-height:1.45}main{max-width:480px;margin:0 auto}"
        "a{color:#70d6ff}h1{color:#4fc3f7;margin:24px 0 6px;font-size:28px}"
        "h2{font-size:18px;margin:28px 0 8px}.lede{color:#b9c3d8;margin:0 0 20px}"
        ".connection{display:flex;gap:12px;align-items:flex-start;background:#0f0f1a;"
        "padding:16px;border-radius:14px;margin:18px 0}.connection strong,.connection span{display:block}"
        ".connection span:last-child{color:#b9c3d8;margin-top:2px;overflow-wrap:anywhere}"
        ".dot{width:10px;height:10px;border-radius:50%%;background:#8a94a8;margin-top:6px;flex:none}"
        ".connected .dot{background:#55d98b}.working .dot{background:#ffd166;animation:pulse 1.4s ease-in-out infinite}"
        ".error .dot{background:#ff7043}.live{font-size:12px;color:#91a0bb;margin-top:8px}"
        ".actions{display:flex;flex-wrap:wrap;gap:8px;margin:16px 0}.actions form{margin:0}"
        "button{padding:10px 16px;background:#4fc3f7;color:#07111a;border:0;border-radius:8px;"
        "font-weight:700;cursor:pointer}button:hover{background:#70d6ff}button:focus-visible,a:focus-visible{outline:3px solid #fff;outline-offset:3px}"
        "button:disabled{background:#596275;color:#c7ccda;cursor:wait}.danger{background:#ff8a65}"
        ".device{background:#0f0f1a;padding:12px 14px;border-radius:12px;margin:8px 0;"
        "display:flex;gap:12px;justify-content:space-between;align-items:center}.device span{overflow-wrap:anywhere}"
        ".empty,.hint{color:#b9c3d8}.technical{margin-top:28px;color:#91a0bb;font-size:13px}"
        ".technical summary{cursor:pointer;color:#b9c3d8}@keyframes pulse{50%%{opacity:.35;transform:scale(.75)}}"
        "@media(prefers-reduced-motion:reduce){.working .dot{animation:none}}"
        "</style>%s</head><body><main>"
        "<a href='/'>← Back to Dial settings</a>"
        "<h1>BLE Media Remote</h1>"
        "<p class='lede'>Connect one physical Bluetooth remote to control media on this Dial.</p>"
        "<div class='connection %s' role='status' aria-live='polite'>"
        "<span class='dot' aria-hidden='true'></span><div><strong>%s</strong><span>%s</span>"
        "%s</div></div>"
        "<div class='actions'>"
        "<form method='POST' action='/ble-enable'>"
        "<input type='hidden' name='enabled' value='%d'>"
        "<button type='submit' class='%s'>%s</button></form>",
        auto_updates
            ? "<script>if(location.search)history.replaceState(null,'','/ble');"
              "setTimeout(function(){if(!document.hidden)location.reload()},1000);"
              "document.addEventListener('visibilitychange',function(){if(!document.hidden)location.reload()});</script>"
            : "",
        state_class, state_title, state_detail,
        auto_updates ? "<span class='live'>Updates automatically</span>" : "",
        status.enabled ? 0 : 1,
        status.enabled ? "danger" : "",
        status.enabled ? "Turn off BLE" : "Turn on BLE");
    if (pos < 0 || pos >= (int)html_size) {
        free(html);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Page generation failed");
        return ESP_FAIL;
    }

    if (status.enabled) {
        if (status.bonded) {
            pos += snprintf(
                html + pos, html_size - (size_t)pos,
                " <form method='POST' action='/ble-forget'>"
                "<button type='submit' class='danger'>Forget remote</button></form>");
        }
        pos += snprintf(html + pos, html_size - (size_t)pos, "</div>");

        if (status.bonded) {
            pos += snprintf(
                html + pos, html_size - (size_t)pos,
                "<p class='hint'>To pair a different remote, forget this one first.</p>");
        } else {
            pos += snprintf(
                html + pos, html_size - (size_t)pos,
                "<h2>Pair a remote</h2>"
                "<p class='hint'>Put the remote into pairing mode, then start a scan.</p>"
                "<form method='POST' action='/ble-scan'>"
                "<button type='submit'%s>%s</button></form>",
                status.state == RK_BLE_HID_HOST_STATE_SCANNING ? " disabled" : "",
                status.state == RK_BLE_HID_HOST_STATE_SCANNING
                    ? "Scanning…" : "Scan for remotes");

            if (status.state != RK_BLE_HID_HOST_STATE_SCANNING) {
            for (size_t i = 0; i < result_count && pos < (int)html_size; i++) {
                char escaped_result[RK_BLE_HID_HOST_NAME_MAX_LEN * 2];
                html_escape(results[i].name, escaped_result,
                            sizeof(escaped_result));
                pos += snprintf(
                    html + pos, html_size - (size_t)pos,
                    "<div class='device'><span>%s</span>"
                    "<form method='POST' action='/ble-pair'>"
                    "<input type='hidden' name='idx' value='%u'>"
                    "<input type='hidden' name='generation' value='%lu'>"
                    "<button type='submit'>Pair this remote</button></form></div>",
                    escaped_result, (unsigned)i,
                    (unsigned long)scan_generation);
            }
                if (result_count == 0 && scan_generation > 0) {
                    pos += snprintf(
                        html + pos, html_size - (size_t)pos,
                        "<p class='empty'>No remotes found. Keep the remote in pairing mode and scan again.</p>");
                }
            }
        }
    } else {
        pos += snprintf(
            html + pos, html_size - (size_t)pos,
            "</div><p class='hint'>BLE stays off across restarts until you turn it on here.</p>");
    }

    if (pos < 0 || pos >= (int)html_size) {
        pos = (int)html_size - 1;
    }
    pos += snprintf(
        html + pos, html_size - (size_t)pos,
        "<details class='technical'><summary>About this setting</summary>"
        "<p>The Dial connects to a separate Bluetooth media remote. The Dial itself "
        "does not appear as a remote to phones or computers.</p>%s%s%s</details>"
        "</main></body></html>",
        status.last_error != RK_BLE_HID_HOST_ERROR_NONE ? "<p>Technical error: " : "",
        status.last_error != RK_BLE_HID_HOST_ERROR_NONE
            ? rk_ble_hid_host_error_name(status.last_error) : "",
        status.last_error != RK_BLE_HID_HOST_ERROR_NONE ? "</p>" : "");
    if (pos < 0 || pos >= (int)html_size) {
        pos = (int)html_size - 1;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, pos);
    free(html);
    return ESP_OK;
}

static esp_err_t ble_enable_handler(httpd_req_t *req) {
    char buf[32] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char enabled_text[4] = {0};
    if (!get_form_field(buf, "enabled", enabled_text, sizeof(enabled_text)) ||
        (strcmp(enabled_text, "0") != 0 && strcmp(enabled_text, "1") != 0)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid enabled value");
        return ESP_FAIL;
    }
    rk_ble_hid_host_result_t result =
        rk_ble_hid_host_set_enabled(enabled_text[0] == '1');
    if (result != RK_BLE_HID_HOST_OK) {
        return send_conflict(req, rk_ble_hid_host_result_name(result));
    }
    return redirect_to_ble(req);
}

static esp_err_t ble_scan_handler(httpd_req_t *req) {
    rk_ble_hid_host_result_t result = rk_ble_hid_host_scan_start();
    if (result != RK_BLE_HID_HOST_OK) {
        return send_conflict(req, rk_ble_hid_host_result_name(result));
    }
    return redirect_to_ble(req);
}

static esp_err_t ble_pair_handler(httpd_req_t *req) {
    char buf[96] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char idx_text[8] = {0};
    char generation_text[16] = {0};
    if (!get_form_field(buf, "idx", idx_text, sizeof(idx_text)) ||
        !get_form_field(buf, "generation", generation_text,
                        sizeof(generation_text))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing scan identity");
        return ESP_FAIL;
    }

    char *end = NULL;
    long idx = strtol(idx_text, &end, 10);
    if (end == idx_text || *end != '\0' || idx < 0 ||
        idx >= RK_BLE_HID_HOST_MAX_RESULTS) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
        return ESP_FAIL;
    }
    end = NULL;
    unsigned long requested_generation = strtoul(generation_text, &end, 10);
    if (end == generation_text || *end != '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid generation");
        return ESP_FAIL;
    }

    rk_ble_hid_host_device_t results[RK_BLE_HID_HOST_MAX_RESULTS];
    uint32_t current_generation = 0;
    size_t count = rk_ble_hid_host_scan_results_copy(
        results, RK_BLE_HID_HOST_MAX_RESULTS, &current_generation);
    if ((size_t)idx >= count || requested_generation != current_generation) {
        return send_conflict(req, "Scan results changed; scan again");
    }

    rk_ble_hid_host_result_t result = rk_ble_hid_host_pair(&results[idx]);
    if (result != RK_BLE_HID_HOST_OK) {
        return send_conflict(req, rk_ble_hid_host_result_name(result));
    }
    return redirect_to_ble(req);
}

static esp_err_t ble_forget_handler(httpd_req_t *req) {
    rk_ble_hid_host_result_t result = rk_ble_hid_host_forget();
    if (result != RK_BLE_HID_HOST_OK) {
        return send_conflict(req, rk_ble_hid_host_result_name(result));
    }
    return redirect_to_ble(req);
}

void config_server_start(void) {
    if (!http_server_lifecycle_lock()) {
        ESP_LOGE(TAG, "Could not acquire HTTP lifecycle lock");
        return;
    }
    if (http_server_lifecycle_owner_locked() ==
        HTTP_SERVER_OWNER_CAPTIVE_PORTAL) {
        ESP_LOGW(TAG, "Config server start suppressed while AP owns port 80");
        http_server_lifecycle_unlock();
        return;
    }
    if (s_server) {
        ESP_LOGW(TAG, "Config server already running");
        http_server_lifecycle_claim_locked(HTTP_SERVER_OWNER_CONFIG);
        http_server_lifecycle_unlock();
        return;
    }

    http_server_lifecycle_claim_locked(HTTP_SERVER_OWNER_CONFIG);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 12;
    config.stack_size = 8192;  // Increased for mDNS resolution during config save
    // Note: max_req_hdr_len set via CONFIG_HTTPD_MAX_REQ_HDR_LEN in sdkconfig

    ESP_LOGI(TAG, "Starting config server on port %d", config.server_port);

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        http_server_lifecycle_release_locked(HTTP_SERVER_OWNER_CONFIG);
        http_server_lifecycle_unlock();
        return;
    }

    // Register URI handlers
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = config_get_handler,
    };
    httpd_register_uri_handler(s_server, &root);

    httpd_uri_t config_post = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
    };
    httpd_register_uri_handler(s_server, &config_post);

    httpd_uri_t wifi_add = {
        .uri = "/wifi-add",
        .method = HTTP_POST,
        .handler = wifi_add_handler,
    };
    httpd_register_uri_handler(s_server, &wifi_add);

    httpd_uri_t wifi_remove = {
        .uri = "/wifi-remove",
        .method = HTTP_POST,
        .handler = wifi_remove_handler,
    };
    httpd_register_uri_handler(s_server, &wifi_remove);

    httpd_uri_t ble_get = {
        .uri = "/ble",
        .method = HTTP_GET,
        .handler = ble_get_handler,
    };
    httpd_register_uri_handler(s_server, &ble_get);

    httpd_uri_t ble_enable = {
        .uri = "/ble-enable",
        .method = HTTP_POST,
        .handler = ble_enable_handler,
    };
    httpd_register_uri_handler(s_server, &ble_enable);

    httpd_uri_t ble_scan = {
        .uri = "/ble-scan",
        .method = HTTP_POST,
        .handler = ble_scan_handler,
    };
    httpd_register_uri_handler(s_server, &ble_scan);

    httpd_uri_t ble_pair = {
        .uri = "/ble-pair",
        .method = HTTP_POST,
        .handler = ble_pair_handler,
    };
    httpd_register_uri_handler(s_server, &ble_pair);

    httpd_uri_t ble_forget = {
        .uri = "/ble-forget",
        .method = HTTP_POST,
        .handler = ble_forget_handler,
    };
    httpd_register_uri_handler(s_server, &ble_forget);

    ESP_LOGI(TAG, "Config server started");
    http_server_lifecycle_unlock();
}

void config_server_stop_locked(void) {
    if (!s_server) {
        http_server_lifecycle_release_locked(HTTP_SERVER_OWNER_CONFIG);
        return;
    }

    ESP_LOGI(TAG, "Stopping config server");
    httpd_stop(s_server);
    s_server = NULL;
    http_server_lifecycle_release_locked(HTTP_SERVER_OWNER_CONFIG);
}

void config_server_stop(void) {
    if (!http_server_lifecycle_lock()) {
        ESP_LOGE(TAG, "Could not acquire HTTP lifecycle lock");
        return;
    }
    config_server_stop_locked();
    http_server_lifecycle_unlock();
}

bool config_server_is_running(void) {
    if (!http_server_lifecycle_lock()) {
        return false;
    }
    bool running = s_server != NULL;
    http_server_lifecycle_unlock();
    return running;
}
