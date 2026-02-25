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

// Favicon data URI (same icon as unified-hifi-control)
static const char *FAVICON_LINK =
    "<link rel='icon' type='image/x-icon' href='data:image/x-icon;base64,"
    "AAABAAEAICAAAAEAIACoEAAAFgAAACgAAAAgAAAAQAAAAAEAIAAAAAAAABAAACMuAAAjLgAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAATU1NIVBTU1xXWVuMYGJlom1ucKJ8fn6OioqKYJKSkiMAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "SU5OMU9UVKdRVlnxVVlc/1pcWf9jY1v/bGxk/31+e/+TlZf/qqyu8r2+vqm+vr4zAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAATFBQclFYW/NSVFT/WE40/2tTGf96WQ3/gV0L/4JeDv+AXhX/emAn/4V5XP+4t7X/"
    "2tvd9djY2Hv///8BAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAE5SUYxTWFv/VE4+/3FTEP+IYQP/kWYG/5NoCv+Xaw//nnAa/6d7"
    "LP+ugzb/oncn/4poJf+on43/6Ovu/9/f35eqqqqDAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABMTU5yVFhb/1dONP+BWwb/j2UG/45lCP+QZwv/"
    "lWsO/5htEP+ecRb/p3kg/7aINv/GnVj/w5tW/5lvIf+gk3j/5+rs/9TU1IEAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAATFFRTlJVV/VWUDz/glwG/49l"
    "B/+MZAr/kmsW/5dwHP+WbRP/nncf/6N5IP+sgSj/soIm/7yLMv/Ln1L/y6Vj/5htHv+lm4f/"
    "1NXW+ri4uD0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFNUVKNY"
    "V1lW/3NVEP+QZgf/jWQK/45lD/+xroH/wq6A/5duFP/TwZr/vqJk/8KkYf/IrW3/4Myj/72L"
    "Lf/JnU7/vpZO/4toIv+0sq7/tra1uoCAgAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAATVVVIVhcXvNdUjb/jGMG/49mC/+QZw//kmgQ/5ZvGf+acx7/mG0U/6F7KP+1klD/"
    "rIQv/66BKP+1hy3/toYj/7mHKv+/lUv/oXUi/4h5V/+pq6z6j4+PMAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAABaXV1gX2Nk/29WHP+TaAv/k2kQ/5RqEv+VaxH/lmsR/5Zs"
    "Ef+YbRL/mnAW/7SSUP+keRz/p3gY/6t7Gv+vfh7/sH8g/7CCK/+ofCn/f2Ii/5eZmf+Mjo5z"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAGZnZ49lZ2P/e1oS/5ZsEv+XbBX/"
    "mG0U/5puFf+abhT/mm4T/5ltEf+dcxr/tZJQ/6R5Iv+idBX/pXcY/6Z4GP+neBr/pngb/6F0"
    "G/+EXxH/gIB6/4GEhKIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAdHV1pW1s"
    "ZP+BXQ//mm8Y/51yG/+fcxv/oHMZ/6J2H/+jeCT/qIAx/6R7KP+xjUn/pnwp/6R4If+idhz/"
    "pXoj/6BzGP+gdBn/m24T/4lhDP90cmb/dXl7twAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAACDg4SmenduvIJeD/+idyX/sYg9/7OKPf+1jEH/to5F/7aQSv+6l1b/q4Q6/62H"
    "P/+xjEb/r4tE/6yGPf+ogTP/qII1/6iCNv+abxj/hl8J/2tpXv9qbm+3AAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAACQEJCPJYOMF/+AXhX/qXwq/7CAJv+zgiP/toUp/7WGLf+0"
    "hCz/sYc3/7KNRv+rgzX/rog+/59zG/+dchrlm8P/5ZrEf+WaxH/k2gL/4BdDP9iY13/YWVl"
    "oQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACcnJpdpKWm/3pfJ/+whTb/vo05"
    "/8ONLf/Fjy3/wY0t/7iGKf+xgy3/upVS/6uBL/+fchf/nHAV/5ltEv+Waw//lGoN/5RpD/+S"
    "Zwf/c1cX/1tfYf9ZW1twAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAJmZmR69"
    "vb7xiX1i/6R5Kv/RpF7/05k4/9WaNf/PlzT/wo4v/7WGLP+/m1n/rYIw/6F0Gv+ccBb/l2wS"
    "/5VqD/+UaQ//kmgN/45kBf9eUTH/VVlb+VNTUysAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAADLy8mhycnH/4hnKf/PpWD/57Jk/+GhO//WnDb/x5Iy/7iILP/DnVv/roEu"
    "/6FzGf+bbxT/lmsQ/5RpD/+SaBH/kWYI/3ZYEP9VVlP/U1VUswAAAAEAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAy8vEJ+rq6/C1r5//mXAk/+G2df/st2j/2Z08/8eR"
    "L/+5hin/vpZQ/6t+Kf+gchj/mm0S/5VqD/+SZxH/kWYM/4NeBv9WTzz/VVZXf1JSTjUAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAOXl5WP7/f//sKaQ/5px"
    "Jv/TqGH/3bBo/8iXQv+3hSv/rX4l/6R2HP+dcBX/lmsQ/5JoD/+RZwv/gl0I/1dOM/9UVlj/"
    "UlJQdgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADy"
    "9PRp+vz+/7q0p/+Kay//pXot/7SJPv+ugTP/pHYg/51vFP+Yaw//k2gK/4tiB/9yVRP/VFBA"
    "/1NXWf9RUVGNAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAD09PRe8fP16dDR0P+Rh2//fWUx/4BgHP+BXhP/gV0Q/3lbE/9sVh//WlI4/1NW"
    "Vf9TVlfyUVNRcgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAANR0tIi1tbWlMTFxuioqqz/j5GP/3t7dv9vb2r/Zmhn/11gY/9YW17s"
    "U1dXn09PTy0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAKamphejo06VlZV9goSElXJ0dZZmaGh/Wl1dUlVVVRsA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAP///wEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAD///////////////////////////8A///8AD//+AAP//AAB//gAAf/wAAD/8AAAf"
    "+AAAH/gAAB/4AAAf+AAAH/gAAB/4AAAf+AAAH/gAAB/8AAAf/AAAP/4AAH//AAB//4AB///AA"
    "///8A///////////////////////3////8='>";

static const char *HTML_SUCCESS_HEAD =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>";

static const char *HTML_SUCCESS_BODY =
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

  char wifi_html[1024] = "";
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
    if (pos >= (int)sizeof(wifi_html)) pos = (int)sizeof(wifi_html) - 1;
  }

  size_t html_size = 10240;  // Extra room for base64 favicon
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
    "</style>"
    "%s"
    "</head><body>"
    "<h1>hiphi frame</h1>"
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
    "<strong>Note:</strong> hiphi frame needs a running bridge service on your network. "
    "See <a href='https://github.com/muness/roon-knob' "
    "target='_blank'>github.com/muness/roon-knob</a> for details."
    "</div></body></html>",
    FAVICON_LINK,
    cfg.wifi_count > 0 ? "<h2>Saved Networks</h2><div class='section'>" : "",
    wifi_html,
    cfg.wifi_count > 0 ? "</div>" : "");

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
  if (!save_ok) {
    ESP_LOGE(TAG, "Failed to save config");
    httpd_resp_send(req,
      "<!DOCTYPE html><html><head>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee;"
      "text-align:center;}h1{color:#4fc3f7;}.error{padding:20px;margin:20px "
      "auto;border-radius:10px;max-width:300px;background:#c62828;}</style></head><body>"
      "<h1>hiphi frame</h1><div class='error'><p><strong>Failed to save WiFi credentials.</strong></p>"
      "<p>Please try again.</p></div></body></html>",
      HTTPD_RESP_USE_STRLEN);
    eink_ui_set_network_status("SAVE FAILED!");
    vTaskDelay(pdMS_TO_TICKS(5000));
    return ESP_FAIL;
  }

  httpd_resp_send_chunk(req, HTML_SUCCESS_HEAD, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, FAVICON_LINK, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, HTML_SUCCESS_BODY, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);

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

  httpd_uri_t configure = {.uri = "/configure", .method = HTTP_POST, .handler = configure_post_handler};
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
    ".device form{display:inline;margin:0;}"
    ;

// ── STA-mode zone picker page (GET /zones) ─────────────────────────────────

static esp_err_t sta_zones_handler(httpd_req_t *req) {
  bridge_zone_t zones[16];
  int count = bridge_client_get_zones(zones, 16);
  char current[64];
  bridge_client_get_current_zone_id(current, sizeof(current));

  char bridge_url[128] = "";
  bridge_client_get_bridge_url(bridge_url, sizeof(bridge_url));

  size_t html_size = 16384;  // Extra room for base64 favicon + zone list
  char *html = malloc(html_size);
  if (!html) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  // HTML-escape bridge_url to prevent attribute injection
  char esc_bridge_url[256] = "";
  if (is_safe_url(bridge_url)) {
    html_escape(bridge_url, esc_bridge_url, sizeof(esc_bridge_url));
  }

  int pos = snprintf(html, html_size,
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi frame - Zones</title>"
    "<style>%s</style>%s</head><body>"
    "<h1>hiphi frame</h1>"
    "<nav><a href='/zones'>Zones</a><a href='/ble'>BLE Remote</a>"
    "%s%s%s"
    "</nav>"
    "<div class='card'><h2>Zone Selection</h2>",
    STA_CSS,
    FAVICON_LINK,
    esc_bridge_url[0] ? "<a href='" : "",
    esc_bridge_url[0] ? esc_bridge_url : "",
    esc_bridge_url[0] ? "' target='_blank'>Bridge Control</a>" : "");
  if (pos >= (int)html_size) pos = (int)html_size - 1;

  if (count == 0) {
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>No zones discovered yet. "
      "Make sure the bridge is running and music is playing.</p>");
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
  char dev_name[64];
  ble_remote_device_name(dev_name, sizeof(dev_name));

  ble_remote_device_t results[BLE_REMOTE_MAX_RESULTS];
  int result_count = ble_remote_get_scan_results(results, BLE_REMOTE_MAX_RESULTS);

  char bridge_url[128] = "";
  bridge_client_get_bridge_url(bridge_url, sizeof(bridge_url));

  size_t html_size = 12288;  // Extra room for base64 favicon
  char *html = malloc(html_size);
  if (!html) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  // HTML-escape bridge_url to prevent attribute injection
  char esc_bridge_url[256] = "";
  if (is_safe_url(bridge_url)) {
    html_escape(bridge_url, esc_bridge_url, sizeof(esc_bridge_url));
  }

  int pos = snprintf(html, html_size,
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>hiphi frame - BLE Remote</title>"
    "<style>%s</style>%s</head><body>"
    "<h1>hiphi frame</h1>"
    "<nav><a href='/zones'>Zones</a><a href='/ble'>BLE Remote</a>"
    "%s%s%s"
    "</nav>"
    "<div class='card'><h2>BLE Media Remote</h2>",
    STA_CSS,
    FAVICON_LINK,
    esc_bridge_url[0] ? "<a href='" : "",
    esc_bridge_url[0] ? esc_bridge_url : "",
    esc_bridge_url[0] ? "' target='_blank'>Bridge Control</a>" : "");
  if (pos >= (int)html_size) pos = (int)html_size - 1;

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
    if (pos >= (int)html_size) pos = (int)html_size - 1;
  } else if (dev_name[0]) {
    char esc_name[128];
    html_escape(dev_name, esc_name, sizeof(esc_name));
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>Paired with <strong>%s</strong> (disconnected, reconnecting...)</p>"
      "<form method='POST' action='/api/ble-unpair'>"
      "<button type='submit' class='btn btn-danger'>Unpair</button>"
      "</form>",
      esc_name);
    if (pos >= (int)html_size) pos = (int)html_size - 1;
  } else {
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>No BLE remote paired.</p>");
    if (pos >= (int)html_size) pos = (int)html_size - 1;
  }

  // Scan
  pos += snprintf(html + pos, html_size - pos,
    "<h2>Find Remotes</h2>");
  if (pos >= (int)html_size) pos = (int)html_size - 1;

  if (scanning) {
    pos += snprintf(html + pos, html_size - pos,
      "<p class='status'>Scanning... <a href='/ble'>Refresh</a></p>");
    if (pos >= (int)html_size) pos = (int)html_size - 1;
  } else {
    pos += snprintf(html + pos, html_size - pos,
      "<form method='POST' action='/api/ble-scan'>"
      "<button type='submit' class='btn'>Scan for Remotes</button>"
      "</form>");
    if (pos >= (int)html_size) pos = (int)html_size - 1;
  }

  // Results
  if (result_count > 0 && !scanning) {
    pos += snprintf(html + pos, html_size - pos, "<h2>Discovered Devices</h2>");
    if (pos >= (int)html_size) pos = (int)html_size - 1;
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
      if (pos >= (int)html_size) pos = (int)html_size - 1;
    }
  }

  pos += snprintf(html + pos, html_size - pos,
    "<p class='status' style='margin-top:20px;font-size:12px;'>"
    "Put your BLE remote into pairing mode before scanning.</p>"
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

  char *endp;
  int idx = (int)strtol(idx_str, &endp, 10);
  if (endp == idx_str || *endp != '\0') {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
    return ESP_FAIL;
  }
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

  httpd_uri_t restart = {.uri = "/api/restart", .method = HTTP_POST, .handler = sta_restart_handler};
  httpd_register_uri_handler(s_server, &restart);

  ESP_LOGI(TAG, "STA web server started (zone picker + BLE config)");
}

void captive_portal_stop(void) {
  if (!s_server) {
    return;
  }

  ESP_LOGI(TAG, "Stopping web server");
  dns_server_stop();  // Safe no-op if DNS was never started (STA mode)
  httpd_stop(s_server);
  s_server = NULL;
}

bool captive_portal_is_running(void) { return s_server != NULL; }
