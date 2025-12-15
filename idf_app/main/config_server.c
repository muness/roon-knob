// HTTP config server - runs when connected to WiFi for remote configuration
// Access at http://<knob-ip>/ to set bridge URL

#include "config_server.h"
#include "platform/platform_storage.h"
#include "roon_client.h"
#include "wifi_manager.h"

#include <string.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "config_server";

static httpd_handle_t s_server = NULL;

// HTML page for config
// Format args: current_bridge, status_class, status_text, bridge_value
static const char *HTML_CONFIG =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Roon Knob Config</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;}"
    "h1{color:#4fc3f7;margin-bottom:5px;}"
    ".info{color:#888;margin:10px 0;}"
    "form{background:#16213e;padding:20px;border-radius:10px;max-width:400px;}"
    "label{display:block;margin:15px 0 5px;color:#aaa;}"
    "input[type=text],input[type=url]{width:100%%;padding:10px;border:1px solid #333;border-radius:5px;background:#0f0f1a;color:#fff;box-sizing:border-box;}"
    "input[type=submit]{padding:12px 24px;margin-top:20px;background:#4fc3f7;color:#000;border:none;border-radius:5px;font-weight:bold;cursor:pointer;}"
    "input[type=submit]:hover{background:#29b6f6;}"
    ".btn-clear{background:#ff7043;}"
    ".btn-clear:hover{background:#ff5722;}"
    ".current{background:#0f0f1a;padding:10px;border-radius:5px;margin:10px 0;font-family:monospace;}"
    ".status{padding:10px;border-radius:5px;margin:10px 0;}"
    ".status-ok{background:#1b5e20;}"
    ".status-warn{background:#e65100;}"
    ".status-err{background:#b71c1c;}"
    ".hint{font-size:12px;color:#666;margin-top:4px;}"
    ".success{background:#2e7d32;padding:15px;border-radius:5px;margin:15px 0;}"
    "</style></head><body>"
    "<h1>Roon Knob</h1>"
    "<p class='info'>Configure your Roon Knob settings</p>"
    "<div class='current'>"
    "<strong>Current Bridge:</strong> %s"
    "</div>"
    "<div class='status %s'>"
    "<strong>Status:</strong> %s"
    "</div>"
    "<form method='POST' action='/config'>"
    "<label>Bridge URL</label>"
    "<input type='url' name='bridge' maxlength='128' placeholder='http://192.168.1.x:8088' value='%s'>"
    "<p class='hint'>Leave empty for mDNS auto-discovery. Check the Roon Knob display for connection progress.</p>"
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
    "<h1>Roon Knob</h1>"
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

    const char *start = strstr(data, search);
    if (!start) {
        return false;
    }
    start += strlen(search);

    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    if (len >= out_len) {
        len = out_len - 1;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    url_decode(out);
    return true;
}

// Handler for GET / - serve the config form
static esp_err_t config_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving config page");

    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    const char *current = cfg.bridge_base[0] ? cfg.bridge_base : "(mDNS auto-discovery)";

    // Get bridge connection status
    const char *status_class;
    char status_text[64];
    bool bridge_connected = roon_client_is_bridge_connected();
    int retry_count = roon_client_get_bridge_retry_count();
    int retry_max = roon_client_get_bridge_retry_max();

    if (bridge_connected) {
        status_class = "status-ok";
        snprintf(status_text, sizeof(status_text), "Connected");
    } else if (!cfg.bridge_base[0]) {
        status_class = "status-warn";
        snprintf(status_text, sizeof(status_text), "Searching via mDNS...");
    } else if (retry_count >= retry_max) {
        status_class = "status-err";
        snprintf(status_text, sizeof(status_text), "Unreachable - check URL or bridge server");
    } else if (retry_count > 0) {
        status_class = "status-warn";
        snprintf(status_text, sizeof(status_text), "Connecting... (%d/%d)", retry_count, retry_max);
    } else {
        status_class = "status-warn";
        snprintf(status_text, sizeof(status_text), "Connecting...");
    }

    // Build HTML with current values and status
    char *html = malloc(2560);
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    snprintf(html, 2560, HTML_CONFIG, current, status_class, status_text, cfg.bridge_base);

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

    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    // Check if Clear button was pressed
    char action[16] = {0};
    get_form_field(buf, "action", action, sizeof(action));

    const char *message;
    if (strcmp(action, "Clear") == 0) {
        cfg.bridge_base[0] = '\0';
        message = "Bridge cleared! Will use mDNS.";
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

        strncpy(cfg.bridge_base, bridge, sizeof(cfg.bridge_base) - 1);
        message = bridge[0] ? "Bridge URL saved!" : "Bridge cleared! Will use mDNS.";
        ESP_LOGI(TAG, "Bridge URL set to: %s", bridge[0] ? bridge : "(mDNS)");
    }

    if (!platform_storage_save(&cfg)) {
        ESP_LOGE(TAG, "Failed to save config");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save");
        return ESP_FAIL;
    }

    // Send success response
    char *html = malloc(1024);
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

void config_server_start(void) {
    if (s_server) {
        ESP_LOGW(TAG, "Config server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 4;
    // Note: max_req_hdr_len set via CONFIG_HTTPD_MAX_REQ_HDR_LEN in sdkconfig

    ESP_LOGI(TAG, "Starting config server on port %d", config.server_port);

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
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

    ESP_LOGI(TAG, "Config server started");
}

void config_server_stop(void) {
    if (!s_server) {
        return;
    }

    ESP_LOGI(TAG, "Stopping config server");
    httpd_stop(s_server);
    s_server = NULL;
}

bool config_server_is_running(void) {
    return s_server != NULL;
}
