#include "captive_portal.h"
#include "dns_server.h"
#include "wifi_manager.h"
#include "controller_config.h"
#include "http_server_lifecycle.h"
#include "ui.h"

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "captive_portal";

static httpd_handle_t s_server = NULL;
static bool s_portal_ready = false;
static portMUX_TYPE s_reboot_schedule_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_reboot_scheduled = false;

typedef struct {
    char ssid[33];
} setup_reboot_context_t;

static void setup_reboot_task(void *arg) {
    setup_reboot_context_t *context = arg;
    char ssid[sizeof(context->ssid)];
    snprintf(ssid, sizeof(ssid), "%s", context->ssid);
    free(context);

    char ssid_status[48];
    snprintf(ssid_status, sizeof(ssid_status), "WiFi: %s", ssid);
    for (int i = 5; i >= 1; i--) {
        char countdown[32];
        snprintf(countdown, sizeof(countdown), "Rebooting in %d...", i);
        ui_update(ssid_status, countdown, false,
                  0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
        ESP_LOGI(TAG, "%s | %s", countdown, ssid_status);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ui_update("Rebooting...", "Please wait", false,
              0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Rebooting now...");
    esp_restart();
    vTaskDelete(NULL);
}

static bool schedule_setup_reboot(const char *ssid) {
    setup_reboot_context_t *context = calloc(1, sizeof(*context));
    if (!context) {
        return false;
    }
    snprintf(context->ssid, sizeof(context->ssid), "%s", ssid ? ssid : "");

    taskENTER_CRITICAL(&s_reboot_schedule_lock);
    if (s_reboot_scheduled) {
        taskEXIT_CRITICAL(&s_reboot_schedule_lock);
        free(context);
        return true;
    }
    s_reboot_scheduled = true;
    taskEXIT_CRITICAL(&s_reboot_schedule_lock);

    if (xTaskCreate(setup_reboot_task, "setup_reboot", 4096, context, 4,
                    NULL) != pdPASS) {
        taskENTER_CRITICAL(&s_reboot_schedule_lock);
        s_reboot_scheduled = false;
        taskEXIT_CRITICAL(&s_reboot_schedule_lock);
        free(context);
        return false;
    }
    return true;
}

static bool register_uri_handler(const httpd_uri_t *uri) {
    esp_err_t err = httpd_register_uri_handler(s_server, uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %s: %s", uri->uri,
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool config_snapshot(controller_config_snapshot_t *out) {
    return controller_config_snapshot(out);
}

static esp_err_t send_unverified_settings(httpd_req_t *req) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req,
        "Settings saved but could not be verified. Keep this setup page open and try again.");
    return ESP_FAIL;
}

static void apply_committed_wifi(bool reconnect) {
    controller_config_wifi_snapshot_t wifi = {0};
    if (controller_config_wifi_snapshot(&wifi)) {
        wifi_mgr_apply_wifi(&wifi, reconnect);
    }
}

// Simple HTML form for WiFi configuration
static const char *HTML_FORM =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HiPhi Dial Setup</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;}"
    "h1{color:#4fc3f7;margin-bottom:5px;}"
    "p{color:#888;margin-top:0;}"
    "form{background:#16213e;padding:20px;border-radius:10px;max-width:300px;}"
    "label{display:block;margin:15px 0 5px;color:#aaa;}"
    "input[type=text],input[type=password],input[type=url]{width:100%;padding:10px;border:1px solid #333;border-radius:5px;background:#0f0f1a;color:#fff;box-sizing:border-box;}"
    "input[type=submit]{width:100%;padding:12px;margin-top:20px;background:#4fc3f7;color:#000;border:none;border-radius:5px;font-weight:bold;cursor:pointer;}"
    "input[type=submit]:hover{background:#29b6f6;}"
    ".status{padding:10px;margin-top:15px;border-radius:5px;}"
    ".success{background:#2e7d32;}"
    ".error{background:#c62828;}"
    ".hint{font-size:12px;color:#666;margin-top:4px;}"
    ".note{background:#1e3a5f;padding:15px;border-radius:10px;max-width:300px;margin-top:20px;font-size:13px;}"
    ".note a{color:#4fc3f7;}"
    ".saved{background:#16213e;padding:12px 20px;border-radius:10px;max-width:300px;margin-top:20px;}"
    ".wifi-entry{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #333;}"
    ".wifi-entry:last-child{border-bottom:0;}"
    ".btn-rm{background:#c62828;color:#fff;border:0;border-radius:5px;padding:7px 10px;cursor:pointer;}"
    "</style></head><body>"
    "<h1>HiPhi Dial</h1>"
    "<p>WiFi Setup</p>"
    "<form method='GET' action='/configure'>"
    "<label>WiFi Network (SSID)</label>"
    "<input type='text' name='ssid' required maxlength='32' placeholder='Your WiFi name'>"
    "<label>Password</label>"
    "<input type='password' name='pass' maxlength='64' placeholder='WiFi password'>"
    "<input type='submit' value='Connect'>"
    "</form>"
    "<div class='note'>"
    "<strong>Note:</strong> HiPhi Dial requires Unified Hi-Fi Control on your network. "
    "It supports Roon, LMS, and OpenHome. See "
    "<a href='https://github.com/open-horizon-labs/unified-hifi-control' "
    "target='_blank'>Unified Hi-Fi Control setup</a>."
    "</div>"
    "</body></html>";

static const char *HTML_SUCCESS =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HiPhi Dial - Saved</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;text-align:center;}"
    "h1{color:#4fc3f7;}"
    ".status{padding:20px;margin:20px auto;border-radius:10px;max-width:300px;background:#2e7d32;}"
    ".next{padding:15px;margin:20px auto;border-radius:10px;max-width:300px;background:#16213e;text-align:left;}"
    ".next li{margin:8px 0;}"
    "</style></head><body>"
    "<h1>HiPhi Dial</h1>"
    "<div class='status'>"
    "<p><strong>WiFi credentials saved!</strong></p>"
    "</div>"
    "<div class='next'>"
    "<p>Next steps:</p>"
    "<ol>"
    "<li>This setup network will disappear in a few seconds</li>"
    "<li>Reconnect your phone to your home WiFi</li>"
    "<li>The HiPhi Dial will connect and start working</li>"
    "</ol>"
    "</div></body></html>";

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
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_len - 1; i++) {
        const char *esc = NULL;
        size_t esc_len = 0;
        switch (src[i]) {
        case '&': esc = "&amp;"; esc_len = 5; break;
        case '<': esc = "&lt;"; esc_len = 4; break;
        case '>': esc = "&gt;"; esc_len = 4; break;
        case '"': esc = "&quot;"; esc_len = 6; break;
        case '\'': esc = "&#39;"; esc_len = 5; break;
        default: break;
        }
        if (esc) {
            if (j + esc_len >= dst_len) break;
            memcpy(dst + j, esc, esc_len);
            j += esc_len;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

// Handler for GET / - serve the config form and recovery removals.
static esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving config form");

    controller_config_snapshot_t snapshot = {0};
    if (!config_snapshot(&snapshot)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings are unavailable");
        return ESP_FAIL;
    }
    const rk_cfg_t *cfg = &snapshot.value;

    httpd_resp_set_type(req, "text/html");
    const char *closing = strstr(HTML_FORM, "</body></html>");
    size_t prefix_len = closing ? (size_t)(closing - HTML_FORM)
                                : strlen(HTML_FORM);
    httpd_resp_send_chunk(req, HTML_FORM, prefix_len);

    if (cfg->wifi_count > 0) {
        httpd_resp_sendstr_chunk(req,
                                "<div class='saved'><strong>Saved Networks</strong>");
        for (int i = 0; i < cfg->wifi_count && i < RK_MAX_WIFI; i++) {
            char escaped[160];
            char row[384];
            html_escape(cfg->wifi[i].ssid, escaped, sizeof(escaped));
            snprintf(row, sizeof(row),
                     "<div class='wifi-entry'><span>%s</span>"
                     "<form method='POST' action='/wifi-remove' style='margin:0'>"
                     "<input type='hidden' name='idx' value='%d'>"
                     "<button type='submit' class='btn-rm'>Remove</button>"
                     "</form></div>",
                     escaped, i);
            httpd_resp_sendstr_chunk(req, row);
        }
        httpd_resp_sendstr_chunk(req, "</div>");
    }

    httpd_resp_sendstr_chunk(req, closing ? closing : "");
    httpd_resp_send_chunk(req, NULL, 0);
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
    if (end == idx_text || *end != '\0' ||
        parsed < 0 || parsed >= RK_MAX_WIFI) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
        return ESP_FAIL;
    }

    controller_config_snapshot_t snapshot = {0};
    if (!config_snapshot(&snapshot)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings are unavailable");
        return ESP_FAIL;
    }
    if (parsed < snapshot.value.wifi_count) {
        ESP_LOGI(TAG, "Removing WiFi '%s' during setup",
                 snapshot.value.wifi[parsed].ssid);
        controller_config_write_result_t result =
            controller_config_remove_wifi((size_t)parsed, NULL);
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

// Handler for GET /configure - save credentials (GET works better in mobile captive portals)
static esp_err_t configure_get_handler(httpd_req_t *req) {
    // Extract query string from URI (after the '?')
    const char *query = strchr(req->uri, '?');
    if (!query || !query[1]) {
        ESP_LOGE(TAG, "No query parameters in: %s", req->uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No parameters provided");
        return ESP_FAIL;
    }
    query++;  // Skip the '?'

    // Copy query string to mutable buffer for parsing
    char buf[384] = {0};
    strncpy(buf, query, sizeof(buf) - 1);
    ESP_LOGI(TAG, "Received config: %s", buf);

    char ssid[33] = {0};
    char pass[65] = {0};

    if (!get_form_field(buf, "ssid", ssid, sizeof(ssid))) {
        ESP_LOGE(TAG, "Missing SSID");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }

    // Password is optional (for open networks)
    get_form_field(buf, "pass", pass, sizeof(pass));

    ESP_LOGI(TAG, "Configuring WiFi: SSID='%s'", ssid);

    // Show "Saving..." on display
    ui_update("Saving...", "", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(500));

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
                          "Two networks are already saved; remove one in Settings first");
        return ESP_FAIL;
    }

    controller_config_write_result_t result =
        controller_config_upsert_wifi(ssid, pass, true, NULL);
    if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to save WiFi credentials");
        return ESP_FAIL;
    }
    if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
        ESP_LOGW(TAG, "WiFi credentials committed but could not be verified");
        /* Keep the setup AP available, but align the derived cache with the
         * authoritative candidate that may now be durable. */
        apply_committed_wifi(false);
        (void)send_unverified_settings(req);
        // Show error on display
        ui_update("VERIFY SETTINGS", "Keep setup open", false,
                  0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(5000));
        return ESP_FAIL;
    }
    /* The proven setup flow responds and then reboots.  Do not tear down the
     * captive HTTP server from inside its own request handler; the next boot
     * will connect with the promoted network. */
    apply_committed_wifi(false);

    // Confirm success only after NVS write and read-back verification.
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_SUCCESS, strlen(HTML_SUCCESS));

    ESP_LOGI(TAG, "Credentials saved, scheduling countdown...");
    if (!schedule_setup_reboot(ssid)) {
        ESP_LOGE(TAG, "Could not schedule setup reboot");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Captive portal redirect - send all unknown requests to root
static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Redirect request: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// iOS captive portal detection - must NOT return "Success"
static esp_err_t ios_captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "iOS captive portal detection: %s", req->uri);
    // Return a redirect to trigger captive portal popup
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Android captive portal detection - must NOT return 204
static esp_err_t android_captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Android captive portal detection: %s", req->uri);
    // Return a redirect to trigger captive portal popup
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

bool captive_portal_start_locked(void) {
    if (http_server_lifecycle_owner_locked() !=
        HTTP_SERVER_OWNER_CAPTIVE_PORTAL) {
        ESP_LOGE(TAG, "Captive portal start attempted without AP port-80 claim");
        return false;
    }
    if (s_server) {
        ESP_LOGW(TAG, "Captive portal already exists (ready: %d)",
                 s_portal_ready);
        return s_portal_ready;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 12;  // root, configure, 4 captive detection, wildcard
    config.stack_size = 8192;  // Increased from default 4096 for NVS + UI operations
    // Note: max_req_hdr_len set via CONFIG_HTTPD_MAX_REQ_HDR_LEN in sdkconfig

    ESP_LOGI(TAG, "Starting captive portal on port %d", config.server_port);

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return false;
    }

    // Register URI handlers
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    if (!register_uri_handler(&root)) goto fail;

    httpd_uri_t configure = {
        .uri = "/configure",
        .method = HTTP_GET,
        .handler = configure_get_handler,
    };
    if (!register_uri_handler(&configure)) goto fail;

    httpd_uri_t wifi_remove = {
        .uri = "/wifi-remove",
        .method = HTTP_POST,
        .handler = wifi_remove_handler,
    };
    if (!register_uri_handler(&wifi_remove)) goto fail;

    // iOS captive portal detection endpoints
    httpd_uri_t ios_hotspot = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = ios_captive_handler,
    };
    if (!register_uri_handler(&ios_hotspot)) goto fail;

    httpd_uri_t ios_success = {
        .uri = "/library/test/success.html",
        .method = HTTP_GET,
        .handler = ios_captive_handler,
    };
    if (!register_uri_handler(&ios_success)) goto fail;

    // Android captive portal detection endpoints
    httpd_uri_t android_generate = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = android_captive_handler,
    };
    if (!register_uri_handler(&android_generate)) goto fail;

    httpd_uri_t android_gen204 = {
        .uri = "/gen_204",
        .method = HTTP_GET,
        .handler = android_captive_handler,
    };
    if (!register_uri_handler(&android_gen204)) goto fail;

    // Redirect all other requests to root (captive portal behavior)
    httpd_uri_t redirect = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };
    if (!register_uri_handler(&redirect)) goto fail;

    // Start DNS server for captive portal detection (phones auto-popup)
    if (!dns_server_start() || !dns_server_is_running()) {
        ESP_LOGE(TAG, "Failed to start captive DNS server");
        goto fail;
    }

    s_portal_ready = true;
    ESP_LOGI(TAG, "Captive portal started with DNS hijacking");
    return true;

fail:
    captive_portal_stop_locked();
    return false;
}

bool captive_portal_start(void) {
    if (!http_server_lifecycle_lock()) {
        return false;
    }
    if (http_server_lifecycle_owner_locked() == HTTP_SERVER_OWNER_CONFIG) {
        http_server_lifecycle_unlock();
        return false;
    }
    http_server_lifecycle_claim_locked(HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
    bool ready = captive_portal_start_locked();
    if (!ready) {
        http_server_lifecycle_release_locked(
            HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
    }
    http_server_lifecycle_unlock();
    return ready;
}

void captive_portal_stop_locked(void) {
    s_portal_ready = false;
    if (!s_server) {
        http_server_lifecycle_release_locked(
            HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
        return;
    }

    ESP_LOGI(TAG, "Stopping captive portal");
    dns_server_stop();
    httpd_stop(s_server);
    s_server = NULL;
    http_server_lifecycle_release_locked(HTTP_SERVER_OWNER_CAPTIVE_PORTAL);
}

void captive_portal_stop(void) {
    if (!http_server_lifecycle_lock()) {
        return;
    }
    captive_portal_stop_locked();
    http_server_lifecycle_unlock();
}

bool captive_portal_is_running_locked(void) {
    return s_server != NULL && s_portal_ready && dns_server_is_running();
}

bool captive_portal_is_running(void) {
    if (!http_server_lifecycle_lock()) {
        return false;
    }
    bool running = captive_portal_is_running_locked();
    http_server_lifecycle_unlock();
    return running;
}
