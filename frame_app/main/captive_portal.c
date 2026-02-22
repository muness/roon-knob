#include "captive_portal.h"
#include "dns_server.h"
#include "platform/platform_storage.h"
#include "rk_cfg.h"
#include "eink_ui.h"
#include "wifi_manager.h"
#include "bridge_client.h"
#include "ble_remote.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "captive_portal";

static httpd_handle_t s_server = NULL;

static const char *HTML_SUCCESS =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi frame - Saved</title>"
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
    "<h1>hiphi frame</h1>"
    "<div class='status'>"
    "<p><strong>WiFi credentials saved!</strong></p>"
    "</div>"
    "<div class='next'>"
    "<p>Next steps:</p>"
    "<ol>"
    "<li>This setup network will disappear in a few seconds</li>"
    "<li>Reconnect your phone to your home WiFi</li>"
    "<li>The hiphi frame will connect and start displaying</li>"
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

// Escape HTML special characters to prevent XSS
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

// Handler for POST /wifi-remove
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
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// Handler for GET / - serve the config form with saved networks
static esp_err_t root_get_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Serving config form");

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

  size_t html_size = 2048;
  char *html = malloc(html_size);
  if (!html) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<h1>hiphi frame</h1><p>Out of memory</p>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  snprintf(html, html_size,
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi frame Setup</title>"
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
    "<h1>hiphi frame</h1>"
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
    "<strong>Note:</strong> hiphi frame needs a running bridge service on your network. "
    "See <a href='https://github.com/muness/roon-knob' "
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

// Handler for GET /configure - save credentials
static esp_err_t configure_get_handler(httpd_req_t *req) {
  const char *query = strchr(req->uri, '?');
  if (!query || !query[1]) {
    ESP_LOGE(TAG, "No query parameters in: %s", req->uri);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No parameters provided");
    return ESP_FAIL;
  }
  query++;

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
  get_form_field(buf, "pass", pass, sizeof(pass));

  ESP_LOGI(TAG, "Configuring WiFi: SSID='%s'", ssid);

  eink_ui_set_network_status("Saving...");
  vTaskDelay(pdMS_TO_TICKS(500));

  rk_cfg_t cfg = {0};
  if (!platform_storage_load(&cfg) || !rk_cfg_is_valid(&cfg)) {
    rk_cfg_set_display_defaults(&cfg);
  }
  rk_cfg_add_wifi(&cfg, ssid, pass);
  strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
  strncpy(cfg.pass, pass, sizeof(cfg.pass) - 1);
  cfg.cfg_ver = RK_CFG_CURRENT_VER;

  bool save_ok = platform_storage_save(&cfg);

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, HTML_SUCCESS, strlen(HTML_SUCCESS));

  if (!save_ok) {
    ESP_LOGE(TAG, "Failed to save config");
    eink_ui_set_network_status("SAVE FAILED!");
    vTaskDelay(pdMS_TO_TICKS(5000));
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Credentials saved, rebooting...");

  char msg[80];
  snprintf(msg, sizeof(msg), "WiFi: %s\nRebooting...", ssid);
  eink_ui_set_network_status(msg);
  vTaskDelay(pdMS_TO_TICKS(3000));

  esp_restart();
  return ESP_OK;
}

// Captive portal redirect
static esp_err_t captive_redirect_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Redirect request: %s", req->uri);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t ios_captive_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "iOS captive portal detection: %s", req->uri);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t android_captive_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Android captive portal detection: %s", req->uri);
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
  config.max_uri_handlers = 12;
  config.stack_size = 8192;

  ESP_LOGI(TAG, "Starting captive portal on port %d", config.server_port);

  if (httpd_start(&s_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return;
  }

  httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
  httpd_register_uri_handler(s_server, &root);

  httpd_uri_t configure = {.uri = "/configure", .method = HTTP_GET, .handler = configure_get_handler};
  httpd_register_uri_handler(s_server, &configure);

  httpd_uri_t wifi_remove = {.uri = "/wifi-remove", .method = HTTP_POST, .handler = wifi_remove_handler};
  httpd_register_uri_handler(s_server, &wifi_remove);

  httpd_uri_t ios_hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = ios_captive_handler};
  httpd_register_uri_handler(s_server, &ios_hotspot);

  httpd_uri_t ios_success = {.uri = "/library/test/success.html", .method = HTTP_GET, .handler = ios_captive_handler};
  httpd_register_uri_handler(s_server, &ios_success);

  httpd_uri_t android_generate = {.uri = "/generate_204", .method = HTTP_GET, .handler = android_captive_handler};
  httpd_register_uri_handler(s_server, &android_generate);

  httpd_uri_t android_gen204 = {.uri = "/gen_204", .method = HTTP_GET, .handler = android_captive_handler};
  httpd_register_uri_handler(s_server, &android_gen204);

  httpd_uri_t redirect = {.uri = "/*", .method = HTTP_GET, .handler = captive_redirect_handler};
  httpd_register_uri_handler(s_server, &redirect);

  dns_server_start();

  ESP_LOGI(TAG, "Captive portal started with DNS hijacking");
}

// ── Common CSS for STA-mode pages ──────────────────────────────────────────

static const char *STA_CSS =
    "body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;}"
    "h1{color:#4fc3f7;margin-bottom:5px;}"
    "h2{color:#aaa;font-size:16px;margin-top:20px;}"
    "a{color:#4fc3f7;}"
    "nav{margin:10px 0 20px;}"
    "nav a{margin-right:15px;text-decoration:none;}"
    ".card{background:#16213e;padding:15px 20px;border-radius:10px;max-width:400px;margin:10px 0;}"
    ".zone{display:flex;justify-content:space-between;align-items:center;"
    "padding:10px;margin:5px 0;border-radius:5px;background:#0f0f1a;cursor:pointer;}"
    ".zone:hover{background:#1e3a5f;}"
    ".zone.active{border:1px solid #4fc3f7;}"
    ".zone form{display:inline;margin:0;}"
    ".btn{padding:8px 16px;background:#4fc3f7;color:#000;border:none;"
    "border-radius:5px;font-weight:bold;cursor:pointer;}"
    ".btn:hover{background:#29b6f6;}"
    ".btn-danger{background:#ff7043;}"
    ".btn-danger:hover{background:#ff5722;}"
    ".status{color:#aaa;margin:10px 0;}"
    ".device{display:flex;justify-content:space-between;align-items:center;"
    "padding:10px;margin:5px 0;border-radius:5px;background:#0f0f1a;}"
    ;

// ── STA-mode zone picker page (GET /zones) ─────────────────────────────────

static esp_err_t sta_zones_handler(httpd_req_t *req) {
  bridge_zone_t zones[16];
  int count = bridge_client_get_zones(zones, 16);
  const char *current = bridge_client_get_current_zone_id();

  char bridge_url[128] = "";
  bridge_client_get_bridge_url(bridge_url, sizeof(bridge_url));

  size_t html_size = 4096;
  char *html = malloc(html_size);
  if (!html) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  int pos = snprintf(html, html_size,
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi frame - Zones</title>"
    "<style>%s</style></head><body>"
    "<h1>hiphi frame</h1>"
    "<nav><a href='/zones'>Zones</a><a href='/ble'>BLE Remote</a>"
    "%s%s%s"
    "</nav>"
    "<div class='card'><h2>Zone Selection</h2>",
    STA_CSS,
    bridge_url[0] ? "<a href='" : "",
    bridge_url[0] ? bridge_url : "",
    bridge_url[0] ? "' target='_blank'>Bridge Control</a>" : "");

  if (count == 0) {
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>No zones discovered yet. "
      "Make sure the bridge is running and music is playing.</p>");
  } else {
    for (int i = 0; i < count; i++) {
      char esc_name[128], esc_id[128];
      html_escape(zones[i].name, esc_name, sizeof(esc_name));
      html_escape(zones[i].id, esc_id, sizeof(esc_id));
      bool is_current = current && strcmp(zones[i].id, current) == 0;
      pos += snprintf(html + pos, html_size - pos,
        "<div class='zone%s'>"
        "<span>%s%s</span>"
        "<form method='POST' action='/api/zone'>"
        "<input type='hidden' name='zone_id' value='%s'>"
        "<button type='submit' class='btn'%s>Select</button>"
        "</form></div>",
        is_current ? " active" : "",
        esc_name,
        is_current ? " (current)" : "",
        esc_id,
        is_current ? " disabled" : "");
    }
  }

  pos += snprintf(html + pos, html_size - pos, "</div></body></html>");

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html, pos);
  free(html);
  return ESP_OK;
}

// ── STA-mode zone selection (POST /api/zone) ───────────────────────────────

static esp_err_t sta_zone_set_handler(httpd_req_t *req) {
  char buf[128] = {0};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  char zone_id[64] = {0};
  if (!get_form_field(buf, "zone_id", zone_id, sizeof(zone_id))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing zone_id");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Web UI: selecting zone '%s'", zone_id);
  bridge_client_set_zone(zone_id);

  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/zones");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// ── STA-mode BLE config page (GET /ble) ─────────────────────────────────────

static esp_err_t sta_ble_handler(httpd_req_t *req) {
  bool connected = ble_remote_is_connected();
  bool scanning = ble_remote_is_scanning();
  const char *dev_name = ble_remote_device_name();

  ble_remote_device_t results[BLE_REMOTE_MAX_RESULTS];
  int result_count = ble_remote_get_scan_results(results, BLE_REMOTE_MAX_RESULTS);

  char bridge_url[128] = "";
  bridge_client_get_bridge_url(bridge_url, sizeof(bridge_url));

  size_t html_size = 4096;
  char *html = malloc(html_size);
  if (!html) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  int pos = snprintf(html, html_size,
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi frame - BLE Remote</title>"
    "<style>%s</style></head><body>"
    "<h1>hiphi frame</h1>"
    "<nav><a href='/zones'>Zones</a><a href='/ble'>BLE Remote</a>"
    "%s%s%s"
    "</nav>"
    "<div class='card'><h2>BLE Media Remote</h2>",
    STA_CSS,
    bridge_url[0] ? "<a href='" : "",
    bridge_url[0] ? bridge_url : "",
    bridge_url[0] ? "' target='_blank'>Bridge Control</a>" : "");

  // Current status
  if (connected && dev_name[0]) {
    char esc_name[128];
    html_escape(dev_name, esc_name, sizeof(esc_name));
    pos += snprintf(html + pos, html_size - pos,
      "<div class='device'>"
      "<span>Connected: <strong>%s</strong></span>"
      "<form method='POST' action='/api/ble-unpair'>"
      "<button type='submit' class='btn btn-danger'>Unpair</button>"
      "</form></div>",
      esc_name);
  } else if (dev_name[0]) {
    char esc_name[128];
    html_escape(dev_name, esc_name, sizeof(esc_name));
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>Paired with <strong>%s</strong> (disconnected, reconnecting...)</p>"
      "<form method='POST' action='/api/ble-unpair'>"
      "<button type='submit' class='btn btn-danger'>Unpair</button>"
      "</form>",
      esc_name);
  } else {
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>No BLE remote paired.</p>");
  }

  // Scan
  pos += snprintf(html + pos, html_size - pos,
    "<h2>Find Remotes</h2>");

  if (scanning) {
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>Scanning... <a href='/ble'>Refresh</a></p>");
  } else {
    pos += snprintf(html + pos, html_size - pos,
      "<form method='POST' action='/api/ble-scan'>"
      "<button type='submit' class='btn'>Scan for Remotes</button>"
      "</form>");
  }

  // Results
  if (result_count > 0 && !scanning) {
    pos += snprintf(html + pos, html_size - pos, "<h2>Discovered Devices</h2>");
    for (int i = 0; i < result_count; i++) {
      char esc_name[128];
      html_escape(results[i].name, esc_name, sizeof(esc_name));
      pos += snprintf(html + pos, html_size - pos,
        "<div class='device'>"
        "<span>%s</span>"
        "<form method='POST' action='/api/ble-pair'>"
        "<input type='hidden' name='idx' value='%d'>"
        "<button type='submit' class='btn'>Pair</button>"
        "</form></div>",
        esc_name, i);
    }
  }

  pos += snprintf(html + pos, html_size - pos,
    "<p class='status' style='margin-top:20px;font-size:12px;'>"
    "Put your BLE remote into pairing mode before scanning.</p>"
    "</div></body></html>");

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html, pos);
  free(html);
  return ESP_OK;
}

// ── BLE API handlers ────────────────────────────────────────────────────────

static esp_err_t sta_ble_scan_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Web UI: starting BLE scan");
  ble_remote_scan_start();
  // Redirect back after short delay to allow scan to start
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/ble");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t sta_ble_pair_handler(httpd_req_t *req) {
  char buf[32] = {0};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  char idx_str[8] = {0};
  if (!get_form_field(buf, "idx", idx_str, sizeof(idx_str))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing idx");
    return ESP_FAIL;
  }

  int idx = atoi(idx_str);
  ESP_LOGI(TAG, "Web UI: pairing with device %d", idx);
  ble_remote_pair(idx);

  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/ble");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t sta_ble_unpair_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Web UI: unpairing BLE remote");
  ble_remote_unpair();

  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/ble");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// ── STA-mode root redirect ──────────────────────────────────────────────────

static esp_err_t sta_root_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/zones");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// ── STA-mode web server (runs when connected to WiFi) ───────────────────────

void captive_portal_start_sta(void) {
  if (s_server) {
    ESP_LOGW(TAG, "Web server already running");
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;
  config.stack_size = 8192;

  ESP_LOGI(TAG, "Starting STA web server on port %d", config.server_port);

  if (httpd_start(&s_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return;
  }

  httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = sta_root_handler};
  httpd_register_uri_handler(s_server, &root);

  httpd_uri_t zones = {.uri = "/zones", .method = HTTP_GET, .handler = sta_zones_handler};
  httpd_register_uri_handler(s_server, &zones);

  httpd_uri_t zone_set = {.uri = "/api/zone", .method = HTTP_POST, .handler = sta_zone_set_handler};
  httpd_register_uri_handler(s_server, &zone_set);

  httpd_uri_t ble = {.uri = "/ble", .method = HTTP_GET, .handler = sta_ble_handler};
  httpd_register_uri_handler(s_server, &ble);

  httpd_uri_t ble_scan = {.uri = "/api/ble-scan", .method = HTTP_POST, .handler = sta_ble_scan_handler};
  httpd_register_uri_handler(s_server, &ble_scan);

  httpd_uri_t ble_pair = {.uri = "/api/ble-pair", .method = HTTP_POST, .handler = sta_ble_pair_handler};
  httpd_register_uri_handler(s_server, &ble_pair);

  httpd_uri_t ble_unpair = {.uri = "/api/ble-unpair", .method = HTTP_POST, .handler = sta_ble_unpair_handler};
  httpd_register_uri_handler(s_server, &ble_unpair);

  ESP_LOGI(TAG, "STA web server started (zone picker + BLE config)");
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
