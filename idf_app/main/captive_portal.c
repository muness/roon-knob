#include "captive_portal.h"
#include "dns_server.h"
#include "platform/platform_storage.h"
#include "rk_cfg.h"
#include "ui.h"
#include "wifi_manager.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

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
    "input[type=text],input[type=password],input[type=url]{width:100%;padding:"
    "10px;border:1px solid "
    "#333;border-radius:5px;background:#0f0f1a;color:#fff;box-sizing:border-"
    "box;}"
    "input[type=submit]{width:100%;padding:12px;margin-top:20px;background:#"
    "4fc3f7;color:#000;border:none;border-radius:5px;font-weight:bold;cursor:"
    "pointer;}"
    "input[type=submit]:hover{background:#29b6f6;}"
    ".status{padding:10px;margin-top:15px;border-radius:5px;}"
    ".success{background:#2e7d32;}"
    ".error{background:#c62828;}"
    ".hint{font-size:12px;color:#666;margin-top:4px;}"
    ".note{background:#1e3a5f;padding:15px;border-radius:10px;max-width:300px;"
    "margin-top:20px;font-size:13px;}"
    ".note a{color:#4fc3f7;}"
    "</style></head><body>"
    "<h1>Roon Knob</h1>"
    "<p>WiFi Setup</p>"
    "<form method='GET' action='/configure'>"
    "<label>WiFi Network (SSID)</label>"
    "<input type='text' name='ssid' required maxlength='32' placeholder='Your "
    "WiFi name'>"
    "<label>Password</label>"
    "<input type='password' name='pass' maxlength='64' placeholder='WiFi "
    "password'>"
    "<input type='submit' value='Connect'>"
    "</form>"
    "<div class='note'>"
    "<strong>Note:</strong> To use this with Roon, you'll need to set up the "
    "Roon Bridge. "
    "See <a href='https://github.com/muness/roon-knob' "
    "target='_blank'>github.com/muness/roon-knob</a> for details."
    "</div>"
    "</body></html>";

static const char *HTML_SUCCESS =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Roon Knob - Saved</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;"
    "text-align:center;}"
    "h1{color:#4fc3f7;}"
    ".status{padding:20px;margin:20px "
    "auto;border-radius:10px;max-width:300px;background:#2e7d32;}"
    ".next{padding:15px;margin:20px "
    "auto;border-radius:10px;max-width:300px;background:#16213e;text-align:"
    "left;}"
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
static bool get_form_field(const char *data, const char *field, char *out,
                           size_t out_len) {
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

// Escape HTML special characters to prevent XSS from rogue AP names
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
    if (esc && j + esc_len < dst_len) {
      memcpy(dst + j, esc, esc_len);
      j += esc_len;
    } else if (!esc) {
      dst[j++] = src[i];
    }
  }
  dst[j] = '\0';
}

// Handler for POST /wifi-remove - remove a saved network
static esp_err_t wifi_remove_handler(httpd_req_t *req) {
  char buf[64] = {0};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  char idx_str[8] = {0};
  if (!get_form_field(buf, "idx", idx_str, sizeof(idx_str))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing index");
    return ESP_FAIL;
  }
  char *endp;
  int idx = (int)strtol(idx_str, &endp, 10);
  if (endp == idx_str || *endp != '\0') {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
    return ESP_FAIL;
  }
  rk_cfg_t cfg = {0};
  platform_storage_load(&cfg);
  if (idx >= 0 && idx < cfg.wifi_count) {
    ESP_LOGI(TAG, "Removing WiFi: '%s'", cfg.wifi[idx].ssid);
    rk_cfg_remove_wifi(&cfg, idx);
    platform_storage_save(&cfg);
  }
  // Redirect back to root
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// Handler for GET / - serve the config form with saved networks
static esp_err_t root_get_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Serving config form");

  // Build saved networks HTML
  rk_cfg_t cfg = {0};
  platform_storage_load(&cfg);

  char wifi_html[512] = "";
  int pos = 0;
  for (int i = 0; i < cfg.wifi_count && i < RK_MAX_WIFI; i++) {
    char escaped[128];
    html_escape(cfg.wifi[i].ssid, escaped, sizeof(escaped));
    pos += snprintf(wifi_html + pos, sizeof(wifi_html) - pos,
        "<div class='wifi-entry'>"
        "<span>%s</span>"
        "<form method='POST' action='/wifi-remove' style='display:inline;margin:0;'>"
        "<input type='hidden' name='idx' value='%d'>"
        "<button type='submit' class='btn-rm'>Remove</button>"
        "</form></div>",
        escaped, i);
  }

  // Build full page with saved networks section
  size_t html_size = 2048;
  char *html = malloc(html_size);
  if (!html) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, strlen(HTML_FORM));
    return ESP_OK;
  }

  snprintf(html, html_size,
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Roon Knob Setup</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;}"
    "h1{color:#4fc3f7;margin-bottom:5px;}"
    "h2{color:#aaa;font-size:16px;margin-top:20px;}"
    "p{color:#888;margin-top:0;}"
    "form{background:#16213e;padding:20px;border-radius:10px;max-width:300px;}"
    "label{display:block;margin:15px 0 5px;color:#aaa;}"
    "input[type=text],input[type=password]{width:100%%;padding:10px;border:1px solid "
    "#333;border-radius:5px;background:#0f0f1a;color:#fff;box-sizing:border-box;}"
    "input[type=submit]{width:100%%;padding:12px;margin-top:20px;background:#4fc3f7;"
    "color:#000;border:none;border-radius:5px;font-weight:bold;cursor:pointer;}"
    "input[type=submit]:hover{background:#29b6f6;}"
    ".wifi-entry{background:#0f0f1a;padding:8px 12px;border-radius:5px;margin:4px 0;"
    "display:flex;justify-content:space-between;align-items:center;max-width:300px;}"
    ".btn-rm{color:#ff7043;text-decoration:none;font-size:13px;}"
    ".btn-rm:hover{color:#ff5722;}"
    ".section{max-width:300px;}"
    ".note{background:#1e3a5f;padding:15px;border-radius:10px;max-width:300px;"
    "margin-top:20px;font-size:13px;}"
    ".note a{color:#4fc3f7;}"
    "</style></head><body>"
    "<h1>Roon Knob</h1>"
    "<p>WiFi Setup</p>"
    "%s%s%s"
    "<form method='GET' action='/configure'>"
    "<h2>Connect to WiFi</h2>"
    "<label>WiFi Network (SSID)</label>"
    "<input type='text' name='ssid' required maxlength='32' placeholder='Your WiFi name'>"
    "<label>Password</label>"
    "<input type='password' name='pass' maxlength='64' placeholder='WiFi password'>"
    "<input type='submit' value='Connect'>"
    "</form>"
    "<div class='note'>"
    "<strong>Note:</strong> To use this with Roon, you'll need to set up the "
    "Roon Bridge. See <a href='https://github.com/muness/roon-knob' "
    "target='_blank'>github.com/muness/roon-knob</a> for details."
    "</div></body></html>",
    cfg.wifi_count > 0 ? "<h2>Saved Networks</h2><div class='section'>" : "",
    wifi_html,
    cfg.wifi_count > 0 ? "</div>" : "");

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html, strlen(html));
  free(html);
  return ESP_OK;
}

// Handler for GET /configure - save credentials (GET works better in mobile
// captive portals)
static esp_err_t configure_get_handler(httpd_req_t *req) {
  // Extract query string from URI (after the '?')
  const char *query = strchr(req->uri, '?');
  if (!query || !query[1]) {
    ESP_LOGE(TAG, "No query parameters in: %s", req->uri);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No parameters provided");
    return ESP_FAIL;
  }
  query++; // Skip the '?'

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
  ui_set_network_status("Saving...");
  vTaskDelay(pdMS_TO_TICKS(500));

  // Load current config, update WiFi credentials, save
  rk_cfg_t cfg = {0};
  if (!platform_storage_load(&cfg) || !rk_cfg_is_valid(&cfg)) {
    // Fresh device — apply display defaults (rotation, timeouts, etc.)
    rk_cfg_set_display_defaults(&cfg);
  }
  // Add to wifi list (or update if SSID already exists)
  rk_cfg_add_wifi(&cfg, ssid, pass);
  // Set active credentials for immediate connection
  strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
  strncpy(cfg.pass, pass, sizeof(cfg.pass) - 1);
  cfg.cfg_ver = RK_CFG_CURRENT_VER;

  bool save_ok = platform_storage_save(&cfg);

  // Send HTTP response first (so browser doesn't show error)
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, HTML_SUCCESS, strlen(HTML_SUCCESS));

  if (!save_ok) {
    ESP_LOGE(TAG, "Failed to save config");
    // Show error on display
    ui_set_network_status("SAVE FAILED!\nCheck serial log");
    vTaskDelay(pdMS_TO_TICKS(5000));
    // Don't reboot - let user see the error
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Credentials saved, showing countdown...");

  // Countdown display
  char ssid_status[48];
  snprintf(ssid_status, sizeof(ssid_status), "WiFi: %s", ssid);

  for (int i = 5; i >= 1; i--) {
    char countdown[32];
    snprintf(countdown, sizeof(countdown), "Rebooting in %d...", i);

    char msg[80];
    snprintf(msg, sizeof(msg), "WiFi: %s\n%s", ssid, countdown);
    ui_set_network_status(msg);
    ESP_LOGI(TAG, "%s | %s", countdown, ssid_status);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  // Final message before reboot
  ui_set_network_status("Rebooting...\nPlease wait");
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGI(TAG, "Rebooting now...");
  esp_restart();

  return ESP_OK; // Never reached
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

void captive_portal_start(void) {
  if (s_server) {
    ESP_LOGW(TAG, "Captive portal already running");
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers =
      12; // root, configure, 4 captive detection, wildcard
  config.stack_size =
      8192; // Increased from default 4096 for NVS + UI operations
  // Note: max_req_hdr_len set via CONFIG_HTTPD_MAX_REQ_HDR_LEN in sdkconfig

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
      .method = HTTP_GET,
      .handler = configure_get_handler,
  };
  httpd_register_uri_handler(s_server, &configure);

  httpd_uri_t wifi_remove = {
      .uri = "/wifi-remove",
      .method = HTTP_POST,
      .handler = wifi_remove_handler,
  };
  httpd_register_uri_handler(s_server, &wifi_remove);

  // iOS captive portal detection endpoints
  httpd_uri_t ios_hotspot = {
      .uri = "/hotspot-detect.html",
      .method = HTTP_GET,
      .handler = ios_captive_handler,
  };
  httpd_register_uri_handler(s_server, &ios_hotspot);

  httpd_uri_t ios_success = {
      .uri = "/library/test/success.html",
      .method = HTTP_GET,
      .handler = ios_captive_handler,
  };
  httpd_register_uri_handler(s_server, &ios_success);

  // Android captive portal detection endpoints
  httpd_uri_t android_generate = {
      .uri = "/generate_204",
      .method = HTTP_GET,
      .handler = android_captive_handler,
  };
  httpd_register_uri_handler(s_server, &android_generate);

  httpd_uri_t android_gen204 = {
      .uri = "/gen_204",
      .method = HTTP_GET,
      .handler = android_captive_handler,
  };
  httpd_register_uri_handler(s_server, &android_gen204);

  // Redirect all other requests to root (captive portal behavior)
  httpd_uri_t redirect = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = captive_redirect_handler,
  };
  httpd_register_uri_handler(s_server, &redirect);

  // Start DNS server for captive portal detection (phones auto-popup)
  dns_server_start();

  ESP_LOGI(TAG, "Captive portal started with DNS hijacking");
}

void captive_portal_stop(void) {
  if (!s_server) {
    return;
  }

  ESP_LOGI(TAG, "Stopping captive portal");
  dns_server_stop();
  httpd_stop(s_server);
  s_server = NULL;
}

bool captive_portal_is_running(void) { return s_server != NULL; }
