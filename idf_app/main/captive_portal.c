#include "captive_portal.h"
#include "wifi_manager.h"
#include "platform/platform_storage.h"

#include <string.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "captive_portal";

static httpd_handle_t s_server = NULL;

// Simple HTML form for WiFi configuration
static const char *HTML_FORM =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Roon Knob Setup</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;}"
    "h1{color:#4fc3f7;margin-bottom:5px;}"
    "p{color:#888;margin-top:0;}"
    "form{background:#16213e;padding:20px;border-radius:10px;max-width:300px;}"
    "label{display:block;margin:15px 0 5px;color:#aaa;}"
    "input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid #333;border-radius:5px;background:#0f0f1a;color:#fff;box-sizing:border-box;}"
    "input[type=submit]{width:100%;padding:12px;margin-top:20px;background:#4fc3f7;color:#000;border:none;border-radius:5px;font-weight:bold;cursor:pointer;}"
    "input[type=submit]:hover{background:#29b6f6;}"
    ".status{padding:10px;margin-top:15px;border-radius:5px;}"
    ".success{background:#2e7d32;}"
    ".error{background:#c62828;}"
    "</style></head><body>"
    "<h1>Roon Knob</h1>"
    "<p>WiFi Setup</p>"
    "<form method='POST' action='/configure'>"
    "<label>WiFi Network (SSID)</label>"
    "<input type='text' name='ssid' required maxlength='32' placeholder='Your WiFi name'>"
    "<label>Password</label>"
    "<input type='password' name='pass' maxlength='64' placeholder='WiFi password'>"
    "<input type='submit' value='Connect'>"
    "</form></body></html>";

static const char *HTML_SUCCESS =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Roon Knob - Saved</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;text-align:center;}"
    "h1{color:#4fc3f7;}"
    ".status{padding:20px;margin:20px auto;border-radius:10px;max-width:300px;background:#2e7d32;}"
    ".next{padding:15px;margin:20px auto;border-radius:10px;max-width:300px;background:#16213e;text-align:left;}"
    ".next li{margin:8px 0;}"
    "</style></head><body>"
    "<h1>Roon Knob</h1>"
    "<div class='status'>"
    "<p><strong>WiFi credentials saved!</strong></p>"
    "</div>"
    "<div class='next'>"
    "<p>Next steps:</p>"
    "<ol>"
    "<li>This setup network will disappear in a few seconds</li>"
    "<li>Reconnect your phone to your home WiFi</li>"
    "<li>The Roon Knob will connect and start working</li>"
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
static esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving config form");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, strlen(HTML_FORM));
    return ESP_OK;
}

// Handler for POST /configure - save credentials
static esp_err_t configure_post_handler(httpd_req_t *req) {
    char buf[256] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);

    if (received <= 0) {
        ESP_LOGE(TAG, "Failed to receive POST data");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }
    buf[received] = '\0';
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

    // Load current config, update WiFi credentials, save
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.pass, pass, sizeof(cfg.pass) - 1);
    cfg.cfg_ver = 1;  // Mark as configured

    if (!platform_storage_save(&cfg)) {
        ESP_LOGE(TAG, "Failed to save config");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save");
        return ESP_FAIL;
    }

    // Send success response
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_SUCCESS, strlen(HTML_SUCCESS));

    ESP_LOGI(TAG, "Credentials saved, switching to STA mode in 2 seconds...");

    // Delay to let HTTP response complete before stopping AP
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Now switch to STA mode with new credentials
    wifi_mgr_reconnect(&cfg);

    return ESP_OK;
}

// Captive portal redirect - send all unknown requests to root
static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void captive_portal_start(void) {
    if (s_server) {
        ESP_LOGW(TAG, "Captive portal already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 8;

    ESP_LOGI(TAG, "Starting captive portal on port %d", config.server_port);

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    // Register URI handlers
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(s_server, &root);

    httpd_uri_t configure = {
        .uri = "/configure",
        .method = HTTP_POST,
        .handler = configure_post_handler,
    };
    httpd_register_uri_handler(s_server, &configure);

    // Redirect all other requests to root (captive portal behavior)
    httpd_uri_t redirect = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };
    httpd_register_uri_handler(s_server, &redirect);

    ESP_LOGI(TAG, "Captive portal started");
}

void captive_portal_stop(void) {
    if (!s_server) {
        return;
    }

    ESP_LOGI(TAG, "Stopping captive portal");
    httpd_stop(s_server);
    s_server = NULL;
}

bool captive_portal_is_running(void) {
    return s_server != NULL;
}
