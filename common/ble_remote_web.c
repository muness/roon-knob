// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "ble_remote_web.h"

#include "bridge_client.h"
#include "controller_shortcut_key.h"
#include "portal_brand.h"
#include "rk_ble_hid_host.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ble_remote_web";

static bool read_form(httpd_req_t *req, char *body, size_t capacity) {
  if (!req->content_len || req->content_len >= capacity) return false;
  size_t offset = 0;
  while (offset < req->content_len) {
    int received = httpd_req_recv(req, body + offset, req->content_len - offset);
    if (received <= 0) return false;
    offset += (size_t)received;
  }
  body[offset] = '\0';
  return true;
}

static bool get_form_field(const char *body, const char *field, char *out, size_t size) {
  const size_t length = strlen(field);
  for (const char *part = body; part && *part;) {
    const char *end = strchr(part, '&');
    const size_t count = end ? (size_t)(end - part) : strlen(part);
    if (count > length && part[length] == '=' && !memcmp(part, field, length)) {
      const size_t value = count - length - 1;
      if (value >= size) return false;
      memcpy(out, part + length + 1, value);
      out[value] = '\0';
      return true;
    }
    part = end ? end + 1 : NULL;
  }
  return false;
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

static bool is_safe_url(const char *url) {
  if (!url || !(strncmp(url, "http://", 7) == 0 ||
                strncmp(url, "https://", 8) == 0)) {
    return false;
  }
  const char *host = strstr(url, "://");
  host = host ? host + 3 : NULL;
  if (!host || !*host || *host == '/' || *host == ':') {
    return false;
  }
  for (const char *p = url; *p; ++p) {
    if ((unsigned char)*p <= ' ' || (unsigned char)*p == 0x7f) {
      return false;
    }
  }
  return true;
}

// ── STA-mode BLE config page (GET /ble) ─────────────────────────────────────

static void ble_selected_zone_name(const bridge_zone_t *zones, int count,
                                   const char *current, void *ctx) {
  for (int i = 0; i < count; ++i) {
    if (strcmp(zones[i].id, current) == 0) {
      html_escape(zones[i].name, ctx, 128);
      return;
    }
  }
}

static const char *ble_media_command_name(rk_ble_media_key_t key) {
  switch (key) {
    case RK_BLE_MEDIA_KEY_PLAY_PAUSE: return "Play / pause";
    case RK_BLE_MEDIA_KEY_NEXT_TRACK: return "Next track";
    case RK_BLE_MEDIA_KEY_PREVIOUS_TRACK: return "Previous track";
    case RK_BLE_MEDIA_KEY_VOLUME_UP: return "Volume up";
    case RK_BLE_MEDIA_KEY_VOLUME_DOWN: return "Volume down";
    default: return NULL;
  }
}

static esp_err_t send_ble_json(httpd_req_t *req, cJSON *root) {
  char *body = root ? cJSON_PrintUnformatted(root) : NULL;
  cJSON_Delete(root);
  if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t rc = httpd_resp_sendstr(req, body);
  cJSON_free(body);
  return rc;
}

static esp_err_t sta_ble_shortcuts_handler(httpd_req_t *req) {
  char query[32], value[12];
  long offset = 0;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "offset", value, sizeof(value)) == ESP_OK) {
    char *end;
    offset = strtol(value, &end, 10);
    if (*end || offset < 0 || offset > 100000)
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid offset");
  }
  controller_shortcuts_page_t *page = heap_caps_malloc_prefer(sizeof(*page), 2,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
  if (!page) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  cJSON *root = cJSON_CreateObject();
  if (root && bridge_client_shortcuts_fetch_for_settings((int)offset, page)) {
    cJSON_AddNumberToObject(root, "next", page->next_offset);
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    for (int i = 0; items && i < page->count; ++i) {
      cJSON *item = cJSON_CreateObject();
      if (!item) break;
      cJSON_AddItemToArray(items, item);
      cJSON_AddStringToObject(item, "id", page->items[i].id);
      cJSON_AddStringToObject(item, "name", page->items[i].name);
    }
  } else if (root) {
    cJSON_AddStringToObject(root, "error", "Couldn’t load Library shortcuts from Hi-Fi Control.");
  }
  free(page);
  return send_ble_json(req, root);
}

/* Button names are local to this controller, like the input history. The
 * bridge continues to identify assignments by the stable Consumer usage. */
#define BLE_BUTTON_NAME_BYTES 97

static bool ble_button_custom_name(uint16_t usage, char *name, bool save) {
  char key[12];
  snprintf(key, sizeof(key), "usage_%04x", (unsigned)usage);
  nvs_handle_t handle;
  if (nvs_open("ble_btn_names", save ? NVS_READWRITE : NVS_READONLY, &handle) != ESP_OK)
    return false;
  size_t size = BLE_BUTTON_NAME_BYTES;
  esp_err_t result = save ? nvs_set_str(handle, key, name) :
                           nvs_get_str(handle, key, name, &size);
  if (save && result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}

static esp_err_t sta_ble_bindings_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON *bindings = bridge_client_shortcut_bindings();
  if (!root || !bindings) {
    cJSON_Delete(bindings);
    if (root) cJSON_AddStringToObject(root, "error", "Couldn’t load saved mappings from Hi-Fi Control.");
    return send_ble_json(req, root);
  }
  cJSON *items = cJSON_AddArrayToObject(root, "items");
  if (!items) {
    cJSON_Delete(bindings);
    cJSON_Delete(root);
    return send_ble_json(req, NULL);
  }
  const cJSON *binding;
  cJSON_ArrayForEach(binding, bindings) {
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(binding, "event");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(binding, "shortcut_id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(binding, "shortcut_name");
    if (!cJSON_IsString(event) || !cJSON_IsString(id)) continue;
    const uint16_t usage = controller_shortcut_bindable_usage(event->valuestring);
    if (!usage) continue;
    const char *label = controller_shortcut_key_label(usage);
    char custom[BLE_BUTTON_NAME_BYTES], key[CONTROLLER_SHORTCUT_KEY_NAME_MAX + 1];
    if (!label && ble_button_custom_name(usage, custom, false)) label = custom;
    if (!label) label = controller_shortcut_key_name(usage, key, sizeof(key));
    cJSON *item = cJSON_CreateObject();
    if (!item) {
      cJSON_Delete(bindings);
      cJSON_Delete(root);
      return send_ble_json(req, NULL);
    }
    cJSON_AddItemToArray(items, item);
    cJSON_AddStringToObject(item, "event", event->valuestring);
    cJSON_AddStringToObject(item, "id", id->valuestring);
    cJSON_AddStringToObject(item, "name", cJSON_IsString(name) ? name->valuestring : id->valuestring);
    cJSON_AddStringToObject(item, "label", label);
  }
  cJSON_Delete(bindings);
  return send_ble_json(req, root);
}

static esp_err_t sta_ble_assign_handler(httpd_req_t *req) {
  char type[32], body[768];
  if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
      strcmp(type, "application/json") != 0 || req->content_len >= sizeof(body)) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected a small JSON request");
  }
  size_t received = 0;
  while (received < req->content_len) {
    const int n = httpd_req_recv(req, body + received, req->content_len - received);
    if (n <= 0) return ESP_FAIL;
    received += (size_t)n;
  }
  body[received] = '\0';
  cJSON *request = cJSON_ParseWithOpts(body, NULL, true);
  cJSON *root = cJSON_CreateObject();
  const char *error = "Press a spare remote button first.";
  const cJSON *event = cJSON_GetObjectItemCaseSensitive(request, "event");
  if (root && event) {
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(request, "expected");
    const cJSON *replacement = cJSON_GetObjectItemCaseSensitive(request, "shortcut");
    error = "Choose a saved mapping and a shortcut, or remove the mapping.";
    if (cJSON_IsString(event) && cJSON_IsString(expected) &&
        (cJSON_IsString(replacement) || cJSON_IsNull(replacement))) {
      error = bridge_client_edit_shortcut(event->valuestring, expected->valuestring,
                                         replacement->valuestring);
    }
  } else if (root && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "learn"))) {
    uint32_t started;
    if (rk_ble_hid_host_begin_learn(&started)) {
      cJSON_AddNumberToObject(root, "started", started);
      error = NULL;
    } else error = "Connect the remote before assigning a button.";
  } else if (root) {
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(request, "at");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(request, "shortcut");
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(request, "usage");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(request, "name");
    rk_ble_hid_host_status_t status;
    if (cJSON_IsNumber(at) && at->valuedouble >= 0 && at->valuedouble <= UINT32_MAX &&
        cJSON_IsNumber(key) && key->valuedouble >= 1 && key->valuedouble <= UINT16_MAX &&
        cJSON_IsString(id) &&
        rk_ble_hid_host_status_copy(&status) == RK_BLE_HID_HOST_OK && status.connected) {
      const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
      for (size_t i = 0; i < status.recent_report_count; ++i) {
        const rk_ble_recent_report_t *report = &status.recent_reports[i];
        if (!report->learned || !report->consumer_control || report->length < 2 ||
            at->valuedouble != report->time_ms || now - report->time_ms > 120000) continue;
        const uint16_t usage = report->data[0] | ((uint16_t)report->data[1] << 8);
        if (key->valuedouble != usage) continue;
        char custom[BLE_BUTTON_NAME_BYTES] = "";
        if (name) {
          if (!cJSON_IsString(name) || !name->valuestring[0] ||
              strlen(name->valuestring) >= sizeof(custom) ||
              controller_shortcut_key_label(usage)) {
            error = "Choose a shorter name for this button.";
            break;
          }
          snprintf(custom, sizeof(custom), "%s", name->valuestring);
        }
        error = bridge_client_assign_shortcut(usage, id->valuestring);
        if (!error && custom[0] && !ble_button_custom_name(usage, custom, true)) {
          cJSON_AddStringToObject(root, "notice", "Assignment saved. The button name couldn’t be saved.");
        }
        break;
      }
    }
  }
  if (root && error) cJSON_AddStringToObject(root, "error", error);
  cJSON_Delete(request);
  return send_ble_json(req, root);
}

static esp_err_t sta_ble_input_handler(httpd_req_t *req) {
  rk_ble_hid_host_status_t status;
  if (rk_ble_hid_host_status_copy(&status) != RK_BLE_HID_HOST_OK) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "Bluetooth unavailable");
  }
  cJSON *root = cJSON_CreateObject();
  if (!root) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  cJSON_AddNumberToObject(root, "now", (uint32_t)(esp_timer_get_time() / 1000));
  cJSON_AddBoolToObject(root, "connected", status.connected);
  cJSON *inputs = cJSON_AddArrayToObject(root, "inputs");
  for (size_t i = status.recent_report_count; inputs && i > 0; --i) {
    const rk_ble_recent_report_t *report = &status.recent_reports[i - 1];
    char payload[3 * RK_BLE_HID_HOST_REPORT_BYTES + 1] = "";
    bool nonzero = false;
    for (size_t j = 0; j < report->length; ++j) {
      snprintf(payload + 3 * j, sizeof(payload) - 3 * j, "%02X ", (unsigned)report->data[j]);
      nonzero |= report->data[j] != 0;
    }
    if (!nonzero) continue;
    cJSON *input = cJSON_CreateObject();
    if (!input) break;
    cJSON_AddItemToArray(inputs, input);
    cJSON_AddNumberToObject(input, "at", report->time_ms);
    cJSON_AddBoolToObject(input, "learned", report->learned);
    cJSON_AddBoolToObject(input, "pointer", report->pointer);
    cJSON_AddNumberToObject(input, "report", report->report_id);
    cJSON_AddStringToObject(input, "bytes", payload);
    const char *media = ble_media_command_name(report->media_key);
    if (media) cJSON_AddStringToObject(input, "media", media);
    if (report->consumer_control && report->length >= 2) {
      const uint16_t usage = report->data[0] | ((uint16_t)report->data[1] << 8);
      char key[CONTROLLER_SHORTCUT_KEY_NAME_MAX + 1];
      if (controller_shortcut_key_name(usage, key, sizeof(key))) {
        cJSON_AddNumberToObject(input, "usage", usage);
        const char *label = controller_shortcut_key_label(usage);
        char custom[BLE_BUTTON_NAME_BYTES];
        if (label) cJSON_AddStringToObject(input, "label", label);
        else {
          cJSON_AddBoolToObject(input, "canName", true);
          if (ble_button_custom_name(usage, custom, false))
            cJSON_AddStringToObject(input, "label", custom);
        }
        cJSON_AddStringToObject(input, "key", key);
        cJSON_AddBoolToObject(input, "assignable",
            controller_shortcut_key_class(usage) == CONTROLLER_SHORTCUT_KEY_FREE);
      }
    }
  }
  bridge_shortcut_result_t result;
  if (bridge_client_latest_shortcut_result(&result)) {
    cJSON *outcome = cJSON_AddObjectToObject(root, "outcome");
    if (outcome) {
      cJSON_AddNumberToObject(outcome, "at", result.press_time_ms);
      cJSON_AddNumberToObject(outcome, "usage", result.usage);
      cJSON_AddStringToObject(outcome, "code", result.code);
      cJSON_AddStringToObject(outcome, "message", result.message);
    }
  }
  char *body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t rc = httpd_resp_sendstr(req, body);
  cJSON_free(body);
  return rc;
}

static esp_err_t sta_ble_handler(httpd_req_t *req) {
  rk_ble_hid_host_status_t status = {0};
  if (rk_ble_hid_host_status_copy(&status) != RK_BLE_HID_HOST_OK)
    status.state = RK_BLE_HID_HOST_STATE_UNAVAILABLE;
  const bool ready = status.state == RK_BLE_HID_HOST_STATE_READY;
  const bool scanning = status.state == RK_BLE_HID_HOST_STATE_SCANNING;
  const bool connecting = status.state == RK_BLE_HID_HOST_STATE_CONNECTING;
  const bool busy = scanning || connecting ||
      status.state == RK_BLE_HID_HOST_STATE_STARTING ||
      status.state == RK_BLE_HID_HOST_STATE_STOPPING;
  char query[32] = "";
  char notice[12] = "";
  char watch[4] = "";
  char details[4] = "";
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    httpd_query_key_value(query, "notice", notice, sizeof(notice));
    httpd_query_key_value(query, "watch", watch, sizeof(watch));
    httpd_query_key_value(query, "details", details, sizeof(details));
  }
  const char *dev_name =
      status.active_name[0] ? status.active_name : status.bonded_name;

  rk_ble_hid_host_device_t results[RK_BLE_HID_HOST_MAX_RESULTS];
  uint32_t scan_generation = 0;
  size_t result_count = rk_ble_hid_host_scan_results_copy(
      results, RK_BLE_HID_HOST_MAX_RESULTS, &scan_generation);

  char bridge_url[128] = "";
  bridge_client_get_bridge_url(bridge_url, sizeof(bridge_url));

  const char *settings_path = req->user_ctx;
  const char *zones_path = !strcmp(settings_path, "/") ? "/" : "/zones";
  const size_t html_size = 2048;
  char *html = malloc(html_size);
  if (!html) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  esp_err_t result = ESP_FAIL;
#define BLE_SEND(value) do { \
  if (httpd_resp_sendstr_chunk(req, (value)) != ESP_OK) goto done; \
} while (0)
#define BLE_FORMAT(...) do { \
  int written = snprintf(html, html_size, __VA_ARGS__); \
  if (written < 0 || (size_t)written >= html_size) goto done; \
  BLE_SEND(html); \
} while (0)

  // HTML-escape bridge_url to prevent attribute injection
  char esc_bridge_url[256] = "";
  if (is_safe_url(bridge_url)) {
    html_escape(bridge_url, esc_bridge_url, sizeof(esc_bridge_url));
  }

  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_type(req, "text/html");
  BLE_SEND("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Bluetooth Remote · HiPhi " PORTAL_BRAND_PRODUCT "</title><style>");
  BLE_SEND(PORTAL_BRAND_CSS);
  BLE_FORMAT(
    ".remote-nav{display:flex;flex-wrap:wrap;gap:6px 14px}"
    ".remote-nav a{margin:0;white-space:nowrap}"
    ".remote-nav [aria-current]{font-weight:600}"
    "#remote-status{margin-top:0}"
    "#button-assign-start[hidden],#button-assign-more[hidden]{display:none}"
    ".remote-steps{display:flex;flex-wrap:wrap;gap:4px 28px;margin:0 0 12px;"
    "padding-left:22px;color:var(--pb-muted);font-size:14px}"
    ".remote-steps [aria-current]{color:var(--pb-text);font-weight:600}"
    "</style>%s</head><body><main>"
    "%s"
    "<h1>Bluetooth Remote</h1>"
    "<nav class='remote-nav' aria-label='Device settings'><a href='%s'>Settings</a>"
    "<a href='/ble' aria-current='page'>Bluetooth Remote</a>"
    "<a href='/power-debug'>Power</a>%s%s%s</nav>",
    (busy || strcmp(watch, "1") == 0)
      ? "<script>setTimeout(function(){if(!document.hidden && "
        "document.activeElement===document.body) location.replace('/ble')},1500);"
        "document.addEventListener('visibilitychange',function(){"
        "if(!document.hidden && document.activeElement===document.body) location.replace('/ble')});</script>"
      : "",
    portal_brand_header_html(), settings_path,
    esc_bridge_url[0] ? "<a href='" : "",
    esc_bridge_url[0] ? esc_bridge_url : "",
    esc_bridge_url[0] ? "' target='_blank'>Unified Hi-Fi Control</a>" : "");

  if (strcmp(notice, "changed") == 0 || strcmp(notice, "retry") == 0) {
    BLE_FORMAT(
      "<p role='alert'>%s</p>", strcmp(notice, "changed") == 0
        ? "The scan results changed. Select your remote from the current list."
        : "The action could not start. Check the current status below and try again.");
  }

  char esc_name[128];
  html_escape(dev_name, esc_name, sizeof(esc_name));
  const char *name = esc_name[0] ? esc_name : "Your remote";
  const char *heading;
  const char *description;
  if (status.state == RK_BLE_HID_HOST_STATE_ERROR ||
      status.state == RK_BLE_HID_HOST_STATE_UNAVAILABLE) {
    heading = "Bluetooth unavailable";
    description = "Restart this device from Settings, then return here.";
  } else if (scanning) {
    heading = "Looking for remotes…";
    description = "Keep the remote nearby and in pairing mode.";
  } else if (connecting) {
    heading = status.bonded ? "Connecting to your remote…" : "Pairing your remote…";
    description = "Keep the remote awake and close to this device.";
  } else if (busy) {
    heading = status.enabled ? "Starting Bluetooth…" : "Turning off Bluetooth…";
    description = "This takes a few seconds.";
  } else if (!status.enabled) {
    heading = "Use a remote to control music";
    description = "Turn on Bluetooth to connect a media remote to this device.";
  } else if (status.connected) {
    heading = "Remote connected";
    description = "Press a volume button on the remote to try it.";
  } else if (status.last_error == RK_BLE_HID_HOST_ERROR_SECURITY) {
    heading = "Could not pair with the remote";
    description = status.bonded
      ? "Try Reconnect. If it fails again, forget the remote and pair it again."
      : "Put the remote back in pairing mode, then scan and pair again.";
  } else if (status.bonded) {
    heading = "Remote saved, waiting to connect";
    description = "Press a button on the remote to wake it, then choose Reconnect.";
  } else if (status.last_error == RK_BLE_HID_HOST_ERROR_CONNECT) {
    heading = "Could not connect to the remote";
    description = "Put it back in pairing mode, keep it close, and scan again.";
  } else if (status.last_error == RK_BLE_HID_HOST_ERROR_SCAN) {
    heading = "The scan did not finish";
    description = "Keep the remote in pairing mode and scan again.";
  } else if (scan_generation && !result_count) {
    heading = "No remotes found";
    description = "Put the remote back in pairing mode and scan again.";
  } else {
    heading = result_count ? "Choose your remote" : "Pair a media remote";
    description = result_count
      ? "Select your remote while its pairing light is still flashing."
      : "Put your remote in pairing mode, then scan for it here.";
  }

  const bool selecting = ready && result_count &&
      status.last_error != RK_BLE_HID_HOST_ERROR_CONNECT &&
      status.last_error != RK_BLE_HID_HOST_ERROR_SECURITY &&
      status.last_error != RK_BLE_HID_HOST_ERROR_SCAN;
  if (status.enabled && !status.bonded && !status.connected &&
      (ready || scanning || connecting)) {
    const bool pairing = connecting || selecting;
    BLE_FORMAT(
      "<ol class='remote-steps' aria-label='Pairing steps'>"
      "<li%s>Find remote</li><li%s>Pair remote</li></ol>",
      pairing ? "" : " aria-current='step'", pairing ? " aria-current='step'" : "");
  }

  BLE_FORMAT(
    "<section class='card' aria-labelledby='remote-status'>"
    "<div role='status'><h2 id='remote-status'>%s</h2>%s%s%s<p>%s</p></div>",
    heading, status.bonded ? "<p><strong>" : "", status.bonded ? name : "",
    status.bonded ? "</strong></p>" : "", description);

  if (busy) {
    BLE_FORMAT("<p><a href='/ble'>Refresh status</a></p>");
  } else if (status.state == RK_BLE_HID_HOST_STATE_DISABLED) {
    BLE_FORMAT(
      "<div class='actions'><form method='POST' action='/api/ble-enable'>"
      "<button name='enabled' value='1'>Turn on Bluetooth</button></form></div>");
  } else if (ready && status.bonded) {
    BLE_FORMAT(
      "<div class='actions'><form method='POST' action='/api/ble-enable'>"
      "<button name='enabled' value='1'>Reconnect</button></form></div>");
  } else if (status.connected) {
    char zone[128] = "";
    bridge_client_visit_zones(ble_selected_zone_name, zone);
    BLE_FORMAT(
      "<p><a href='%s'>%s%s</a></p>", zones_path,
      zone[0] ? "Controlling " : "Choose a zone", zone);
    if (status.last_error != RK_BLE_HID_HOST_ERROR_NONE || !status.bonded) {
      BLE_FORMAT(
        "<p role='alert'>This remote could not be saved for next time.</p>");
    }
  }

  if (ready && !status.bonded && status.enabled) {
    if (selecting) {
      for (size_t i = 0; i < result_count; i++) {
        html_escape(results[i].name, esc_name, sizeof(esc_name));
        BLE_FORMAT(
          "<div class='actions'><form method='POST' action='/api/ble-pair'>"
          "<input type='hidden' name='idx' value='%u'>"
          "<input type='hidden' name='generation' value='%lu'>"
          "<button>Pair %s</button></form></div>",
          (unsigned)i, (unsigned long)scan_generation, esc_name);
      }
    }
    BLE_FORMAT(
      "<div class='actions'><form method='POST' action='/api/ble-scan'>"
      "<button>%s</button></form></div>"
      "<details><summary>How do I put my remote in pairing mode?</summary>"
      "<p>Follow the remote maker’s instructions to enter pairing mode, then scan and select it.</p>"
      "<p>Some remotes first appear by Bluetooth address, a string of six pairs of letters and numbers.</p></details>",
      scan_generation ? "Scan again" : "Scan for remotes");
  }
  BLE_FORMAT("</section>");

  {
    BLE_FORMAT(
      "<section class='card' aria-labelledby='button-assign-title'>"
      "<h2 id='button-assign-title'>Button mappings</h2>"
      "<p id='button-assign-list-status' role='status'>Loading saved mappings…</p>"
      "<div id='button-assign-list'></div>"
      "<p id='button-assign-status' role='status' tabindex='-1'>%s</p>"
      "<button id='button-assign-start' type='button'%s>Learn a button</button>"
      "<div id='button-assign-picker' hidden>"
      "<div id='button-assign-name-row' hidden><label for='button-assign-name'>Button name (optional)</label>"
      "<input id='button-assign-name' type='text' maxlength='24' placeholder='e.g. Star'></div>"
      "<label for='button-assign-shortcut'>Library shortcut</label>"
      "<select id='button-assign-shortcut'></select>"
      "<button id='button-assign-more' type='button' hidden>Load more shortcuts</button> "
      "<button id='button-assign-save' type='button'>Save assignment</button></div>"
      "<details id='button-assign-report' hidden><summary>Show report details</summary>"
      "<p><code id='button-assign-bytes'></code></p></details></section>",
      status.connected ? "Choose a spare remote button to play a saved Library shortcut." : "",
      status.connected ? "" : " hidden");
    BLE_SEND("<script>(()=>{"
      "const get=id=>document.getElementById('button-assign-'+id);"
      "const start=get('start'),status=get('status'),picker=get('picker'),"
      "shortcut=get('shortcut'),save=get('save'),more=get('more'),report=get('report');"
      "const name=get('name'),nameRow=get('name-row');let learned,label,next,editing;"
      "const read=async(url,body)=>{const r=await fetch(url,{cache:'no-store',"
      "signal:AbortSignal.timeout(body?12000:6000),...(body?{method:'POST',"
      "headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{})});"
      "if(!r.ok)throw Error('Couldn’t reach the device. Try again.');"
      "const data=await r.json();if(data.error)throw Error(data.error);return data;};"
      "const load=async offset=>{const data=await read('/api/ble-shortcuts?offset='+offset);"
      "for(const item of data.items)shortcut.add(new Option(item.name,item.id));"
      "next=data.next;more.hidden=next<0;save.disabled=!shortcut.value;};"
      "shortcut.onchange=()=>{save.disabled=!shortcut.value;};"
      "const list=get('list'),listStatus=get('list-status');"
      "const setBusy=busy=>{start.disabled=busy;list.inert=busy;};"
      "const change=(item,value)=>read('/api/ble-assign',{event:item.event,expected:item.id,shortcut:value});"
      "const refresh=async()=>{try{const {items}=await read('/api/ble-bindings');"
      "list.replaceChildren();listStatus.textContent=items.length?'':'No saved button mappings.';"
      "for(const item of items){const row=document.createElement('div');"
      "const gesture=item.event.split(':')[1];"
      "const title=item.label+(gesture==='press'?'':' ('+gesture+' press)');"
      "const text=document.createElement('p');text.textContent=title+' → '+item.name;"
      "const edit=document.createElement('button'),remove=document.createElement('button');"
      "edit.type=remove.type='button';edit.textContent='Change shortcut';remove.textContent='Delete mapping';"
      "edit.setAttribute('aria-label','Change shortcut for '+title);"
      "remove.setAttribute('aria-label','Delete mapping for '+title);"
      "edit.onclick=async()=>{setBusy(true);picker.hidden=true;report.hidden=true;learned=null;"
      "editing=item;nameRow.hidden=true;shortcut.replaceChildren(new Option('Choose a shortcut',''));"
      "try{await load(0);if(shortcut.options.length===1){status.textContent='No Library shortcuts available.';return;}"
      "status.textContent='Choose a new shortcut for '+title+'.';picker.hidden=false;shortcut.focus();"
      "}catch(e){status.textContent=e.message;}finally{setBusy(false);}};"
      "remove.onclick=async()=>{if(!confirm('Delete the mapping for '+title+'?'))return;"
      "setBusy(true);picker.hidden=true;report.hidden=true;learned=editing=null;"
      "try{await change(item,null);status.textContent=title+' mapping deleted.';await refresh();status.focus();}"
      "catch(e){status.textContent=e.message;}finally{setBusy(false);}};"
      "row.append(text,edit,' ',remove);list.append(row);}"
      "}catch(e){list.replaceChildren();listStatus.textContent=e.message;}};"
      "refresh();"
      "start.onclick=async()=>{setBusy(true);picker.hidden=true;report.hidden=true;editing=null;"
      "shortcut.replaceChildren();learned=null;more.hidden=true;"
      "try{const {started}=await read('/api/ble-assign',{learn:true});"
      "status.textContent='Press the spare button you want to assign.';"
      "const until=performance.now()+12000;let skipped=false;"
      "while(performance.now()<until){const data=await read('/api/ble-input');"
      "if(!data.connected)throw Error('Remote disconnected. Reconnect it and try again.');"
      "const fresh=data.inputs.filter(i=>i.learned&&((i.at-started)|0)>=0);"
      "const input=fresh.find(i=>i.assignable);"
      "if(!input&&fresh.length){const last=fresh[fresh.length-1];skipped=true;"
      "report.hidden=false;get('bytes').textContent='Report '+last.report+' · '+last.bytes;"
      "const reason=last.pointer?'Pointer input can’t be assigned.':"
      "last.media||last.key?'That button is reserved for playback controls.':"
      "'This input can’t be assigned with the current firmware.';"
      "const hint=reason+' Try another button.';"
      "if(status.textContent!==hint)status.textContent=hint;}"
      "if(input){report.hidden=true;"
      "learned=input;label=input.label||(input.key.startsWith('usage_')?'This button':"
      "input.key[0].toUpperCase()+input.key.slice(1).replaceAll('_',' '));"
      "await load(0);"
      "if(!shortcut.options.length){status.textContent='Pin a shortcut in Hi-Fi Control’s Library, then try again.';return;}"
      "nameRow.hidden=!input.canName;name.value=input.canName?(input.label||''):'';"
      "picker.hidden=false;status.textContent=label+' learned. Choose what it should play.';shortcut.focus();return;}"
      "await new Promise(resolve=>setTimeout(resolve,250));}"
      "if(!skipped)status.textContent='No button received. Try again.';"
      "}catch(e){status.textContent=e.message;}"
      "finally{setBusy(false);start.textContent='Learn another button';}};"
      "more.onclick=async()=>{setBusy(true);more.disabled=true;save.disabled=true;shortcut.disabled=true;try{await load(next);}"
      "catch(e){status.textContent=e.message;}finally{setBusy(false);more.disabled=false;shortcut.disabled=false;save.disabled=!shortcut.value;}};"
      "save.onclick=async()=>{if((!learned&&!editing)||!shortcut.value)return;"
      "setBusy(true);save.disabled=true;more.disabled=true;shortcut.disabled=true;"
      "const title=shortcut.selectedOptions[0].text,custom=learned?.canName?name.value.trim():'';"
      "try{const {notice}=editing?await change(editing,shortcut.value):"
      "await read('/api/ble-assign',{at:learned.at,usage:learned.usage,"
      "shortcut:shortcut.value,...(custom?{name:custom}:{})});"
      "status.textContent=notice||(editing?.label||custom||label)+' assigned to '+title+'.';"
      "picker.hidden=true;learned=editing=null;await refresh();status.focus();"
      "}catch(e){status.textContent=e.message;}"
      "finally{setBusy(false);save.disabled=!shortcut.value;more.disabled=false;shortcut.disabled=false;}};"
      "})();</script>");
  }

  if (status.bonded || status.connected) {
    BLE_FORMAT(
      "<details id='connection-details'%s><summary>Connection details</summary>"
      "<dl><dt>Bluetooth link</dt><dd>%s</dd>"
      "<dt>Saved for next time</dt><dd>%s</dd>%s%s%s</dl>"
      "<h3>Recent remote input</h3>",
      strcmp(details, "1") == 0 ? " open" : "",
      status.connected ? "Connected" : connecting ? "Connecting" : "Disconnected",
      status.bonded ? "Yes" : "No",
      status.last_error == RK_BLE_HID_HOST_ERROR_NONE ? "" : "<dt>Diagnostic code</dt><dd><code>",
      status.last_error == RK_BLE_HID_HOST_ERROR_NONE ? "" :
        rk_ble_hid_host_error_name(status.last_error),
      status.last_error == RK_BLE_HID_HOST_ERROR_NONE ? "" : "</code></dd>");

    if (status.recent_report_count == 0) {
      BLE_FORMAT(
        "<p>No remote input recorded yet. Try a button on the remote.</p>");
    } else {
      BLE_FORMAT(
        "<p>Received by this device, newest first.</p><ol>");
      for (size_t i = 0; i < status.recent_report_count; ++i) {
        const rk_ble_recent_report_t *report = &status.recent_reports[i];
        const char *command = ble_media_command_name(report->media_key);
        char custom[BLE_BUTTON_NAME_BYTES], escaped[6 * BLE_BUTTON_NAME_BYTES];
        if (!command && report->consumer_control && report->length >= 2) {
          const uint16_t usage = report->data[0] | ((uint16_t)report->data[1] << 8);
          command = controller_shortcut_key_label(usage);
          if (!command && ble_button_custom_name(usage, custom, false)) {
            html_escape(custom, escaped, sizeof(escaped));
            command = escaped;
          }
        }
        char payload[3 * RK_BLE_HID_HOST_REPORT_BYTES + 1] = "";
        for (size_t j = 0; j < report->length; ++j) {
          snprintf(payload + 3 * j, sizeof(payload) - 3 * j,
                   "%02X ", (unsigned)report->data[j]);
        }
        BLE_FORMAT(
          "<li>%s <small>· Report %u · <code>%s</code></small></li>",
          command ? command : report->pointer ? "Pointer input" :
          report->consumer_control ? "Consumer control input" : "Unmapped input",
          (unsigned)report->report_id, payload);
      }
      BLE_FORMAT("</ol>");
    }
    BLE_FORMAT(
      "<p><a href='/ble?details=1#connection-details'>Refresh input history</a></p></details>");
  }

  if (!busy && status.enabled && (ready || status.connected)) {
    BLE_FORMAT(
      "<details><summary>Manage Bluetooth</summary><div class='actions'>%s"
      "<form method='POST' action='/api/ble-enable'>"
      "<button name='enabled' value='0' class='btn-danger'>Turn off Bluetooth</button>"
      "</form></div></details>",
      status.bonded
        ? "<form method='POST' action='/api/ble-unpair'>"
          "<button class='btn-danger'>Forget remote</button></form>"
        : "");
  }
  BLE_FORMAT("%s</main></body></html>",
                  portal_brand_footer_html());

  result = httpd_resp_send_chunk(req, NULL, 0);
done:
  free(html);
#undef BLE_FORMAT
#undef BLE_SEND
  return result;
}

// ── BLE API handlers ────────────────────────────────────────────────────────

static esp_err_t ble_redirect(httpd_req_t *req, const char *location) {
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", location);
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t sta_ble_scan_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Web UI: starting BLE scan");
  rk_ble_hid_host_result_t result = rk_ble_hid_host_scan_start();
  if (result != RK_BLE_HID_HOST_OK) {
    return ble_redirect(req, "/ble?notice=retry");
  }
  return ble_redirect(req, "/ble?watch=1");
}

static esp_err_t sta_ble_pair_handler(httpd_req_t *req) {
  char buf[32];
  if (!read_form(req, buf, sizeof(buf)))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form");

  char idx_str[8] = {0};
  char generation_str[16] = {0};
  if (!get_form_field(buf, "idx", idx_str, sizeof(idx_str))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing idx");
    return ESP_FAIL;
  }
  if (!get_form_field(buf, "generation", generation_str,
                      sizeof(generation_str))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing generation");
    return ESP_FAIL;
  }

  char *endp = NULL;
  long idx = strtol(idx_str, &endp, 10);
  if (endp == idx_str || *endp != '\0' || idx < 0 ||
      idx >= RK_BLE_HID_HOST_MAX_RESULTS) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid index");
    return ESP_FAIL;
  }

  endp = NULL;
  unsigned long requested_generation = strtoul(generation_str, &endp, 10);
  if (endp == generation_str || *endp != '\0') {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid generation");
    return ESP_FAIL;
  }

  rk_ble_hid_host_device_t results[RK_BLE_HID_HOST_MAX_RESULTS];
  uint32_t current_generation = 0;
  size_t count = rk_ble_hid_host_scan_results_copy(
      results, RK_BLE_HID_HOST_MAX_RESULTS, &current_generation);
  if ((size_t)idx >= count || requested_generation != current_generation) {
    return ble_redirect(req, "/ble?notice=changed");
  }

  ESP_LOGI(TAG, "Web UI: pairing with device %ld", idx);
  rk_ble_hid_host_result_t result = rk_ble_hid_host_pair(&results[idx]);
  if (result != RK_BLE_HID_HOST_OK) {
    return ble_redirect(req, "/ble?notice=retry");
  }

  return ble_redirect(req, "/ble?watch=1");
}

static esp_err_t sta_ble_unpair_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Web UI: forgetting BLE remote");
  rk_ble_hid_host_result_t result = rk_ble_hid_host_forget();
  if (result != RK_BLE_HID_HOST_OK) {
    return ble_redirect(req, "/ble?notice=retry");
  }

  return ble_redirect(req, "/ble?watch=1");
}

static esp_err_t sta_ble_enable_handler(httpd_req_t *req) {
  char buf[32];
  if (!read_form(req, buf, sizeof(buf)))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form");

  char enabled_str[4] = {0};
  if (!get_form_field(buf, "enabled", enabled_str, sizeof(enabled_str)) ||
      (strcmp(enabled_str, "0") != 0 && strcmp(enabled_str, "1") != 0)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid enabled value");
    return ESP_FAIL;
  }

  rk_ble_hid_host_result_t result =
      rk_ble_hid_host_set_enabled(enabled_str[0] == '1');
  if (result != RK_BLE_HID_HOST_OK) {
    return ble_redirect(req, "/ble?notice=retry");
  }

  return ble_redirect(req, "/ble?watch=1");
}

bool ble_remote_web_register(httpd_handle_t server, const char *settings_path) {
  const httpd_uri_t routes[] = {
    {.uri = "/ble", .method = HTTP_GET, .handler = sta_ble_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-input", .method = HTTP_GET, .handler = sta_ble_input_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-shortcuts", .method = HTTP_GET, .handler = sta_ble_shortcuts_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-bindings", .method = HTTP_GET, .handler = sta_ble_bindings_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-assign", .method = HTTP_POST, .handler = sta_ble_assign_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-scan", .method = HTTP_POST, .handler = sta_ble_scan_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-enable", .method = HTTP_POST, .handler = sta_ble_enable_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-pair", .method = HTTP_POST, .handler = sta_ble_pair_handler, .user_ctx = (void *)settings_path},
    {.uri = "/api/ble-unpair", .method = HTTP_POST, .handler = sta_ble_unpair_handler, .user_ctx = (void *)settings_path},
  };
  _Static_assert(sizeof(routes) / sizeof(routes[0]) == BLE_REMOTE_WEB_ROUTE_COUNT,
                 "Update the portal route capacity");
  for (size_t i = 0; i < BLE_REMOTE_WEB_ROUTE_COUNT; ++i) {
    esp_err_t error = httpd_register_uri_handler(server, &routes[i]);
    if (error != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register %s: %s", routes[i].uri, esp_err_to_name(error));
      return false;
    }
  }
  return true;
}
