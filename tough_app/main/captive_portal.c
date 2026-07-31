// captive_portal.h implementation for hiphi tough -- adapted from
// frame_app/main/captive_portal.c. Unlike Frame, this target has no BLE
// media remote (see tough_capabilities.h / #193 / #191), so the BLE
// pairing/config web UI and its API routes do not exist here at all --
// this is not a stub, there is nothing to configure.

#include "captive_portal.h"
#include "dns_server.h"
#include "controller_config.h"
#include "touch_ui.h"
#include "wifi_manager.h"
#include "bridge_client.h"
#include "os_mutex.h"

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "captive_portal";

static httpd_handle_t s_server = NULL;
typedef enum {
  WEB_SERVER_NONE = 0,
  WEB_SERVER_AP_PORTAL,
  WEB_SERVER_STA_CONFIG,
} web_server_mode_t;
static web_server_mode_t s_server_mode = WEB_SERVER_NONE;
static os_mutex_t s_server_lifecycle_lock = OS_MUTEX_INITIALIZER;
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

  char msg[80];
  snprintf(msg, sizeof(msg), "WiFi: %s\nRebooting...", ssid);
  touch_ui_post_network_status(msg);
  vTaskDelay(pdMS_TO_TICKS(3000));
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

#ifdef ESP_PLATFORM
static portMUX_TYPE s_server_lock_init = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_server_lock_storage;

static bool ensure_server_lifecycle_lock(void) {
  bool ready;
  taskENTER_CRITICAL(&s_server_lock_init);
  if (s_server_lifecycle_lock == NULL) {
    s_server_lifecycle_lock = xSemaphoreCreateMutexStatic(
        &s_server_lock_storage);
  }
  ready = s_server_lifecycle_lock != NULL;
  taskEXIT_CRITICAL(&s_server_lock_init);
  return ready;
}
#else
static bool ensure_server_lifecycle_lock(void) { return true; }
#endif

static bool lock_server_lifecycle(void) {
  return ensure_server_lifecycle_lock() &&
         os_mutex_lock(&s_server_lifecycle_lock) == 0;
}

static void unlock_server_lifecycle(void) {
  (void)os_mutex_unlock(&s_server_lifecycle_lock);
}

static void stop_server_locked(void);

static bool register_uri_handler(const httpd_uri_t *uri) {
  esp_err_t err = httpd_register_uri_handler(s_server, uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register %s: %s", uri->uri,
             esp_err_to_name(err));
    return false;
  }
  return true;
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

static const char *HTML_SUCCESS_HEAD =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>";

static const char *HTML_SUCCESS_BODY =
    "<title>hiphi tough - Saved</title>"
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
    "<h1>hiphi tough</h1>"
    "<div class='status'>"
    "<p><strong>WiFi credentials saved!</strong></p>"
    "</div>"
    "<div class='next'>"
    "<p>Next steps:</p>"
    "<ol>"
    "<li>This setup network will disappear in a few seconds</li>"
    "<li>Reconnect your phone to your home WiFi</li>"
    "<li>The hiphi tough will connect and start displaying</li>"
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

  // Search for the field, skipping substring matches like "xssid=" for "ssid="
  const char *start = data;
  while ((start = strstr(start, search)) != NULL) {
    if (start == data || *(start - 1) == '&') break;
    start++;  // Skip this substring match, keep looking
  }
  if (!start) {
    return false;
  }
  start += strlen(search);

  const char *end = strchr(start, '&');
  size_t len = end ? (size_t)(end - start) : strlen(start);

  // Decode before truncating: a 64-character password may occupy up to
  // 192 bytes while URL encoded.
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
    if (esc) {
      if (j + esc_len < dst_len) {
        memcpy(dst + j, esc, esc_len);
        j += esc_len;
      } else {
        break;  // Buffer full, stop rather than drop characters
      }
    } else {
      dst[j++] = src[i];
    }
  }
  dst[j] = '\0';
}

// Validate URL is safe for href embedding (must start with http:// or https://)
static bool is_safe_url(const char *url) {
  return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
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
  if (endp == idx_str || *endp != '\0' ||
      idx < 0 || idx >= RK_MAX_WIFI) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
    return ESP_FAIL;
  }
  controller_config_snapshot_t snapshot = {0};
  if (!controller_config_snapshot(&snapshot)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Settings are unavailable");
    return ESP_FAIL;
  }
  if (idx >= 0 && idx < snapshot.value.wifi_count) {
    ESP_LOGI(TAG, "Removing WiFi: '%s'", snapshot.value.wifi[idx].ssid);
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
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// Handler for GET / - serve the config form with saved networks
static esp_err_t root_get_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Serving config form");

  controller_config_snapshot_t snapshot = {0};
  if (!controller_config_snapshot(&snapshot)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Settings are unavailable");
    return ESP_FAIL;
  }
  const rk_cfg_t *cfg = &snapshot.value;

  char wifi_html[1024] = "";
  int pos = 0;
  for (int i = 0; i < cfg->wifi_count && i < RK_MAX_WIFI; i++) {
    char escaped[128];
    html_escape(cfg->wifi[i].ssid, escaped, sizeof(escaped));
    pos += snprintf(wifi_html + pos, sizeof(wifi_html) - pos,
        "<div class='wifi-entry'>"
        "<span>%s</span>"
        "<form method='POST' action='/wifi-remove' style='display:inline;margin:0;'>"
        "<input type='hidden' name='idx' value='%d'>"
        "<button type='submit' class='btn-rm'>Remove</button>"
        "</form></div>",
        escaped, i);
    if (pos >= (int)sizeof(wifi_html)) pos = (int)sizeof(wifi_html) - 1;
  }

  size_t html_size = 8192;
  char *html = heap_caps_malloc(html_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!html) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<h1>hiphi tough</h1><p>Out of memory</p>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  snprintf(html, html_size,
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi tough Setup</title>"
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
    "</style>"
    "</head><body>"
    "<h1>hiphi tough</h1>"
    "<p>WiFi Setup</p>"
    "%s%s%s"
    "<form method='POST' action='/configure'>"
    "<h2>Connect to WiFi</h2>"
    "<label>WiFi Network (SSID)</label>"
    "<input type='text' name='ssid' required maxlength='32' placeholder='Your WiFi name'>"
    "<label>Password</label>"
    "<input type='password' name='pass' maxlength='64' placeholder='WiFi password'>"
    "<input type='submit' value='Connect'>"
    "</form>"
    "<div class='note'>"
    "<strong>Note:</strong> HiPhi Tough requires Unified Hi-Fi Control on your network. "
    "It supports Roon, LMS, and OpenHome. See "
    "<a href='https://github.com/open-horizon-labs/unified-hifi-control' "
    "target='_blank'>Unified Hi-Fi Control setup</a>."
    "</div></body></html>",
    cfg->wifi_count > 0 ? "<h2>Saved Networks</h2><div class='section'>" : "",
    wifi_html,
    cfg->wifi_count > 0 ? "</div>" : "");

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html, strlen(html));
  free(html);
  return ESP_OK;
}

// Handler for POST /configure - save credentials
static esp_err_t configure_post_handler(httpd_req_t *req) {
  char buf[384] = {0};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  char ssid[33] = {0};
  char pass[65] = {0};

  if (!get_form_field(buf, "ssid", ssid, sizeof(ssid))) {
    ESP_LOGE(TAG, "Missing SSID");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
    return ESP_FAIL;
  }
  get_form_field(buf, "pass", pass, sizeof(pass));

  ESP_LOGI(TAG, "Configuring WiFi: SSID='%s', pass=***", ssid);

  touch_ui_post_network_status("Saving...");
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
                      "Two networks are already saved; remove one first");
    return ESP_FAIL;
  }
  controller_config_write_result_t result =
      controller_config_upsert_wifi(ssid, pass, true, NULL);

  httpd_resp_set_type(req, "text/html");
  if (result == CONTROLLER_CONFIG_NOT_COMMITTED) {
    ESP_LOGE(TAG, "Failed to save config");
    httpd_resp_send(req,
      "<!DOCTYPE html><html><head>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;"
      "text-align:center;}h1{color:#4fc3f7;}.error{padding:20px;margin:20px "
      "auto;border-radius:10px;max-width:300px;background:#c62828;}</style></head><body>"
      "<h1>hiphi tough</h1><div class='error'><p><strong>Failed to save WiFi credentials.</strong></p>"
      "<p>Please try again.</p></div></body></html>",
      HTTPD_RESP_USE_STRLEN);
    touch_ui_post_network_status("SAVE FAILED!");
    vTaskDelay(pdMS_TO_TICKS(5000));
    return ESP_FAIL;
  }
  if (result == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED) {
    ESP_LOGW(TAG, "WiFi credentials committed but could not be verified");
    /* Preserve the setup surface while aligning the derived cache with the
     * authoritative candidate that may now be durable. */
    apply_committed_wifi(false);
    (void)send_unverified_settings(req);
    touch_ui_post_network_status("VERIFY SETTINGS");
    vTaskDelay(pdMS_TO_TICKS(5000));
    return ESP_FAIL;
  }
  /* The proven setup flow responds and then reboots.  Do not tear down the
   * captive HTTP server from inside its own request handler; the next boot
   * will connect with the promoted network. */
  apply_committed_wifi(false);

  httpd_resp_send_chunk(req, HTML_SUCCESS_HEAD, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, HTML_SUCCESS_BODY, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);

  ESP_LOGI(TAG, "Credentials saved, scheduling reboot...");
  if (!schedule_setup_reboot(ssid)) {
    ESP_LOGE(TAG, "Could not schedule setup reboot");
    return ESP_FAIL;
  }
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

bool captive_portal_start(void) {
  if (!lock_server_lifecycle()) {
    return false;
  }
  if (s_server) {
    if (s_server_mode == WEB_SERVER_AP_PORTAL && dns_server_is_running()) {
      unlock_server_lifecycle();
      return true;
    }
    ESP_LOGI(TAG, "Replacing HTTP server mode %d with AP portal",
             s_server_mode);
    stop_server_locked();
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 10;
  config.stack_size = 8192;

  ESP_LOGI(TAG, "Starting captive portal on port %d", config.server_port);

  if (httpd_start(&s_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    unlock_server_lifecycle();
    return false;
  }

  httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
  if (!register_uri_handler(&root)) goto fail;

  httpd_uri_t configure = {.uri = "/configure", .method = HTTP_POST, .handler = configure_post_handler};
  if (!register_uri_handler(&configure)) goto fail;

  httpd_uri_t wifi_remove = {.uri = "/wifi-remove", .method = HTTP_POST, .handler = wifi_remove_handler};
  if (!register_uri_handler(&wifi_remove)) goto fail;

  httpd_uri_t ios_hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = ios_captive_handler};
  if (!register_uri_handler(&ios_hotspot)) goto fail;

  httpd_uri_t ios_success = {.uri = "/library/test/success.html", .method = HTTP_GET, .handler = ios_captive_handler};
  if (!register_uri_handler(&ios_success)) goto fail;

  httpd_uri_t android_generate = {.uri = "/generate_204", .method = HTTP_GET, .handler = android_captive_handler};
  if (!register_uri_handler(&android_generate)) goto fail;

  httpd_uri_t android_gen204 = {.uri = "/gen_204", .method = HTTP_GET, .handler = android_captive_handler};
  if (!register_uri_handler(&android_gen204)) goto fail;

  httpd_uri_t redirect = {.uri = "/*", .method = HTTP_GET, .handler = captive_redirect_handler};
  if (!register_uri_handler(&redirect)) goto fail;

  if (!dns_server_start() || !dns_server_is_running()) {
    ESP_LOGE(TAG, "Failed to start captive DNS server");
    goto fail;
  }

  s_server_mode = WEB_SERVER_AP_PORTAL;
  ESP_LOGI(TAG, "Captive portal started with DNS hijacking");
  unlock_server_lifecycle();
  return true;

fail:
  stop_server_locked();
  unlock_server_lifecycle();
  return false;
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
    ;

// ── STA-mode zone picker page (GET /zones) ─────────────────────────────────

static esp_err_t sta_zones_handler(httpd_req_t *req) {
  bridge_zone_t zones[16];
  int count = bridge_client_get_zones(zones, 16);
  char current[64];
  bridge_client_get_current_zone_id(current, sizeof(current));

  char bridge_url[128] = "";
  bridge_client_get_bridge_url(bridge_url, sizeof(bridge_url));

  size_t html_size = 12288;  // Extra room for zone list
  char *html = heap_caps_malloc(html_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!html) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  char esc_bridge_url[256] = "";
  if (is_safe_url(bridge_url)) {
    html_escape(bridge_url, esc_bridge_url, sizeof(esc_bridge_url));
  }

  int pos = snprintf(html, html_size,
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi tough - Zones</title>"
    "<style>%s</style></head><body>"
    "<h1>hiphi tough</h1>"
    "<nav><a href='/zones'>Zones</a>"
    "%s%s%s"
    "</nav>"
    "<div class='card'><h2>Zone Selection</h2>",
    STA_CSS,
    esc_bridge_url[0] ? "<a href='" : "",
    esc_bridge_url[0] ? esc_bridge_url : "",
    esc_bridge_url[0] ? "' target='_blank'>Unified Hi-Fi Control</a>" : "");
  if (pos >= (int)html_size) pos = (int)html_size - 1;

  if (count == 0) {
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>No zones discovered yet. "
      "Make sure Unified Hi-Fi Control is running and music is playing.</p>");
    if (pos >= (int)html_size) pos = (int)html_size - 1;
  } else {
    for (int i = 0; i < count; i++) {
      char esc_name[128], esc_id[128];
      html_escape(zones[i].name, esc_name, sizeof(esc_name));
      html_escape(zones[i].id, esc_id, sizeof(esc_id));
      bool is_current = current[0] && strcmp(zones[i].id, current) == 0;
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
      if (pos >= (int)html_size) pos = (int)html_size - 1;
    }
  }

  pos += snprintf(html + pos, html_size - pos,
    "</div>"
    "<div class='card' style='margin-top:20px;'>"
    "<form method='POST' action='/api/restart'>"
    "<button type='submit' class='btn btn-danger'>Restart Device</button>"
    "</form></div>"
    "</body></html>");
  if (pos >= (int)html_size) pos = (int)html_size - 1;

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
  if (!bridge_client_set_zone(zone_id)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Zone was not available or could not be saved");
    return ESP_FAIL;
  }

  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/zones");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// ── STA-mode restart handler (POST /api/restart) ────────────────────────────

static esp_err_t sta_restart_handler(httpd_req_t *req) {
  ESP_LOGW(TAG, "Web UI: restart requested");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req,
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;margin:40px;background:#1a1a2e;color:#eee;"
    "text-align:center;}h1{color:#4fc3f7;}</style></head><body>"
    "<h1>Restarting...</h1><p>The device will reconnect in a few seconds.</p>"
    "</body></html>",
    HTTPD_RESP_USE_STRLEN);
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;  // unreachable
}

// ── STA-mode root redirect ──────────────────────────────────────────────────

static esp_err_t sta_root_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/zones");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// ── STA-mode web server (runs when connected to WiFi) ───────────────────────

bool captive_portal_start_sta(void) {
  if (!lock_server_lifecycle()) {
    return false;
  }
  if (s_server) {
    ESP_LOGW(TAG, "Web server already exists (mode: %d)", s_server_mode);
    bool ready = s_server_mode == WEB_SERVER_STA_CONFIG;
    unlock_server_lifecycle();
    return ready;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;
  config.stack_size = 8192;

  ESP_LOGI(TAG, "Starting STA web server on port %d", config.server_port);

  if (httpd_start(&s_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    unlock_server_lifecycle();
    return false;
  }

  httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = sta_root_handler};
  if (!register_uri_handler(&root)) goto fail;

  httpd_uri_t zones = {.uri = "/zones", .method = HTTP_GET, .handler = sta_zones_handler};
  if (!register_uri_handler(&zones)) goto fail;

  httpd_uri_t zone_set = {.uri = "/api/zone", .method = HTTP_POST, .handler = sta_zone_set_handler};
  if (!register_uri_handler(&zone_set)) goto fail;

  httpd_uri_t restart = {.uri = "/api/restart", .method = HTTP_POST, .handler = sta_restart_handler};
  if (!register_uri_handler(&restart)) goto fail;

  s_server_mode = WEB_SERVER_STA_CONFIG;
  ESP_LOGI(TAG, "STA web server started (zone picker)");
  unlock_server_lifecycle();
  return true;

fail:
  stop_server_locked();
  unlock_server_lifecycle();
  return false;
}

static void stop_server_locked(void) {
  if (!s_server) {
    s_server_mode = WEB_SERVER_NONE;
    return;
  }

  ESP_LOGI(TAG, "Stopping web server");
  dns_server_stop();  // Safe no-op if DNS was never started (STA mode)
  httpd_stop(s_server);
  s_server = NULL;
  s_server_mode = WEB_SERVER_NONE;
}

void captive_portal_stop(void) {
  if (!lock_server_lifecycle()) {
    return;
  }
  stop_server_locked();
  unlock_server_lifecycle();
}

bool captive_portal_is_running(void) {
  if (!lock_server_lifecycle()) {
    return false;
  }
  bool running = s_server != NULL &&
                 s_server_mode == WEB_SERVER_AP_PORTAL &&
                 dns_server_is_running();
  unlock_server_lifecycle();
  return running;
}

bool captive_portal_is_sta_running(void) {
  if (!lock_server_lifecycle()) {
    return false;
  }
  bool running = s_server != NULL && s_server_mode == WEB_SERVER_STA_CONFIG;
  unlock_server_lifecycle();
  return running;
}
