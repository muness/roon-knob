/* Shared 360px round touch navigation; hardware and power policy stay target-owned. */
#include "controller_touch_navigation.h"
#include "bridge_client.h"
#include "cJSON.h"
#include "controller_config.h"
#include "controller_library.h"
#include "controller_navigation.h"
#include "lvgl.h"
#include "platform/platform_touch_navigation.h"
#include "portal_brand.h"
#include "platform/platform_identity.h"
#include <ctype.h>
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"
#include "ui.h"
#include "wifi_manager.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BROWSE, PLAY, QUEUE, IMAGE };
#define HISTORY_MAX 8
#define COVER_SIZE 132
#define COVER_BYTES (COVER_SIZE * COVER_SIZE * 2)
typedef struct {
  unsigned generation;
  int kind, offset, item_index;
  char endpoint[128], zone[64], location[513], ref[513], image[600];
} request_t;
typedef struct {
  request_t request;
  controller_library_page_t page;
  char *pixels;
  bool ok;
} result_t;
static QueueHandle_t requests, results;
static bool busy, mutation_locked;
static int busy_kind;
static unsigned generation = 1;
static int screen = CONTROLLER_SCREEN_ARTWORK, selected_index, offset,
           brightness = 100;
static controller_library_page_t *page;
static char endpoint[128], zone[64], location[513];
static struct {
  char location[513];
  int offset, selected_index;
} history[HISTORY_MAX];
static int depth, setting_index, menu_index, zone_index, zone_count,
    action_index;
static int desired_absolute = -1;
static bool connection_refresh_pending;
static uint64_t connection_observed_before;
static int64_t connection_refresh_ms, connection_status_ms;
static bridge_zone_t zone_rows[BRIDGE_CLIENT_MAX_ZONES];
static char folder_name[301] = "Library";
static bool pending_browse, carousel, rebuilding;
static lv_obj_t *scroller, *home_button, *art_touch;
static lv_obj_t *rows[BRIDGE_CLIENT_MAX_ZONES + 1];
static lv_obj_t *cover_views[CONTROLLER_LIBRARY_PAGE_SIZE];
static char history_names[HISTORY_MAX][301];
static lv_obj_t *overlay, *heading, *subtitle, *counter, *back, *primary,
    *secondary;
static struct {
  lv_image_dsc_t descriptor;
  char *pixels;
  bool attempted, failed;
} covers[CONTROLLER_LIBRARY_PAGE_SIZE];
static bool chooser;
static bool settings_about, settings_reset;
static int settings_detail, edit_kind, forget_index = -1;
static char edit_ssid[33], forget_ssid[33], displayed_networks[RK_MAX_WIFI][33];
static lv_obj_t *entry, *detail_label;
static bool recovery_active, recovery_choose_zone, brightness_dirty;
static int64_t brightness_changed_ms, recovery_check_ms;
static char recovery_title[64], recovery_text[600];
static char settings_notice[100];
static void return_to_artwork(void);
static int pending_mutation = -1;
static unsigned pending_mutation_generation;
static char pending_ref[513];
static int64_t mutation_sent_ms;
static void render(void);
static bool submit(int kind, const char *ref, const char *image,
                   int image_index);
static void reset_library(void) {
  generation++;
  depth = 0;
  selected_index = offset = 0;
  location[0] = 0;
  snprintf(folder_name, sizeof(folder_name), "Library");
  pending_mutation = -1;
  desired_absolute = -1;
  chooser = false;
  if (page) {
    memset(page, 0, sizeof(*page));
    page->next_offset = -1;
  }
}
static bool request_current(const request_t *r) {
  char base[128], selected[64];
  return bridge_client_get_request_base(base, sizeof(base)) &&
         bridge_client_get_current_zone_id(selected, sizeof(selected)) &&
         !strcmp(base, r->endpoint) && !strcmp(selected, r->zone);
}
static void worker(void *arg) {
  (void)arg;
  request_t r;
  for (;;) {
    if (xQueueReceive(requests, &r, portMAX_DELAY) != pdTRUE)
      continue;
    result_t *result = heap_caps_calloc(1, sizeof(*result),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!result) { /* Return a small failure sentinel rather than leave the UI
                      busy forever. */
      result = NULL;
      xQueueSend(results, &result, portMAX_DELAY);
      continue;
    }
    result->request = r;
    snprintf(result->page.notice, sizeof(result->page.notice),
             "Connection changed. Tap to refresh.");
    if (request_current(&r)) {
      char url[850], *response = NULL;
      size_t len = 0;
      int rc = -1;
      if (r.kind == IMAGE) {
        snprintf(url, sizeof(url), "%s%s&width=%d&height=%d&format=rgb565",
                 r.endpoint, r.image, COVER_SIZE, COVER_SIZE);
        rc = platform_http_get_bounded(url, COVER_BYTES, &response, &len);
        if (rc == 0 && len == COVER_BYTES && request_current(&r)) {
          result->pixels = response;
          response = NULL;
          result->ok = true;
        }
      } else {
        bool mutation = r.kind == PLAY || r.kind == QUEUE;
        char *body = controller_library_body(
            r.zone, r.location, r.offset, mutation ? r.ref : NULL,
            r.kind == QUEUE ? "queue" : "play");
        snprintf(url, sizeof(url), "%s/api/%s", r.endpoint,
                 mutation ? "play_ref" : "collections");
        controller_connection_t connection;
        bridge_client_connection_snapshot(&connection);
        if (body && controller_connection_ready(&connection) &&
            request_current(&r))
          rc = platform_http_post_json(url, body, &response, &len);
        cJSON_free(body);
        if (rc == 0 && response && len <= PLATFORM_HTTP_JSON_MAX_BYTES &&
            request_current(&r))
          result->ok = controller_library_parse(response, r.zone, mutation,
                                                &result->page);
        else
          snprintf(result->page.notice, sizeof(result->page.notice), "%s",
                   mutation ? "Playback uncertain. Check now playing."
                            : "Library unavailable. Tap to retry.");
      }
      if (r.kind == BROWSE && result->ok && result->page.next_offset >= 0 &&
          (result->page.next_offset <= r.offset || result->page.count == 0)) {
        result->ok = false;
        result->page.count = 0;
        snprintf(result->page.notice, sizeof(result->page.notice),
                 "Invalid library page. Tap to refresh.");
      }
      platform_http_free(response);
    }
    xQueueSend(results, &result, portMAX_DELAY);
  }
}
static void clear_cover(void) {
  for (int i = 0; i < CONTROLLER_LIBRARY_PAGE_SIZE; i++) {
    if (cover_views[i])
      lv_image_set_src(cover_views[i], NULL);
    if (covers[i].pixels) {
      lv_image_cache_drop(&covers[i].descriptor);
      lv_image_header_cache_drop(&covers[i].descriptor);
      platform_http_free(covers[i].pixels);
    }
    memset(&covers[i], 0, sizeof(covers[i]));
  }
}
static void label(lv_obj_t **out, lv_obj_t *parent, int y, int width,
                  const lv_font_t *font) {
  *out = lv_label_create(parent);
  lv_obj_set_width(*out, width);
  lv_obj_align(*out, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_text_font(*out, font, 0);
  lv_obj_set_style_text_color(*out, lv_color_hex(0xeeeeee), 0);
  lv_obj_set_style_text_align(*out, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(*out, LV_LABEL_LONG_DOT);
}
static void button_text(lv_obj_t *button, const char *text) {
  lv_obj_t *child = lv_obj_get_child(button, 0);
  lv_label_set_text(child, text);
  lv_obj_center(child);
}
static void change_screen(int next) {
  screen = next;
  platform_touch_screen_changed((controller_screen_t)screen);
  settings_about = settings_reset = false;
  settings_detail = edit_kind = 0;
  memset(edit_ssid, 0, sizeof(edit_ssid));
  settings_notice[0] = 0;
  ui_set_background_tap_handler(screen == CONTROLLER_SCREEN_NOW_PLAYING
                                    ? return_to_artwork : NULL);
  pending_mutation = -1;
  desired_absolute = -1;
  chooser = false;
  generation++;
  ui_hide_zone_picker();
  ui_set_controls_visible(screen == CONTROLLER_SCREEN_NOW_PLAYING);
  ui_set_artwork_only(screen == CONTROLLER_SCREEN_ARTWORK);
  if (overlay)
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  if (home_button)
    lv_obj_add_flag(home_button, LV_OBJ_FLAG_HIDDEN);
  if (art_touch)
    lv_obj_add_flag(art_touch, LV_OBJ_FLAG_HIDDEN);
  if (screen == CONTROLLER_SCREEN_ZONES) {
    zone_count = bridge_client_get_zones(zone_rows, BRIDGE_CLIENT_MAX_ZONES);
    char current[64] = "";
    bridge_client_get_current_zone_id(current, sizeof(current));
    zone_index = 0;
    for (int i = 0; i < zone_count; i++)
      if (!strcmp(zone_rows[i].id, current))
        zone_index = i;
  }
  if (screen == CONTROLLER_SCREEN_LIBRARY) {
    /* Parent/page changes are deferred while an old network request drains. */
    pending_browse = true;
  }
  render();
}
static void selected_cover(void) {
  if (screen != CONTROLLER_SCREEN_LIBRARY || chooser || !carousel || !page ||
      busy || pending_browse)
    return;
  /* At most 12 * 34,848 bytes in PSRAM; load the visible card and neighbours
   * first. Each image is attempted once per library page, not every render. */
  int candidates[] = {selected_index, selected_index - 1, selected_index + 1};
  for (unsigned n = 0; n < sizeof(candidates) / sizeof(candidates[0]); n++) {
    int i = candidates[n];
    if (i >= 0 && i < page->count && !covers[i].attempted &&
        *page->items[i].image) {
      if (submit(IMAGE, NULL, page->items[i].image, i))
        covers[i].attempted = true;
      return;
    }
  }
}
static void focus_row(int index) {
  if (!scroller || index < 0 || index >= BRIDGE_CLIENT_MAX_ZONES + 1 ||
      !rows[index])
    return;
  for (int i = 0; i < BRIDGE_CLIENT_MAX_ZONES + 1; i++)
    if (rows[i])
      lv_obj_set_style_bg_color(
          rows[i], lv_color_hex(i == index ? 0x39434b : 0x181818), 0);
  lv_obj_scroll_to_view(rows[index], LV_ANIM_ON);
}
static void move_item(int delta) {
  if (pending_browse || (busy && busy_kind != IMAGE) || !page || !page->count)
    return;
  if (chooser) {
    if (!primary || !secondary) return;
    action_index = delta > 0 ? 1 : 0;
    lv_obj_set_style_bg_color(
        primary, lv_color_hex(action_index == 0 ? 0x39434b : 0x252525), 0);
    lv_obj_set_style_bg_color(
        secondary, lv_color_hex(action_index == 1 ? 0x39434b : 0x252525), 0);
    return;
  }
  int next = selected_index + delta;
  if (next >= page->count) {
    if (page->next_offset >= 0) {
      desired_absolute = page->next_offset + next - page->count;
      offset = page->next_offset;
      selected_index = 0;
      pending_browse = true;
      generation++;
      clear_cover();
      render();
    } else {
      selected_index = page->count - 1;
      focus_row(selected_index);
    }
    return;
  }
  if (next < 0) {
    if (offset > 0) {
      desired_absolute = offset + next;
      if (desired_absolute < 0)
        desired_absolute = 0;
      offset = offset > CONTROLLER_LIBRARY_PAGE_SIZE
                   ? offset - CONTROLLER_LIBRARY_PAGE_SIZE
                   : 0;
      selected_index = CONTROLLER_LIBRARY_PAGE_SIZE - 1;
      pending_browse = true;
      generation++;
      clear_cover();
      render();
    } else {
      selected_index = 0;
      focus_row(0);
    }
    return;
  }
  selected_index = next;
  focus_row(selected_index);
  selected_cover();
}
static void back_action(void) {
  pending_mutation = -1;
  desired_absolute = -1;
  if (chooser) {
    chooser = false;
    render();
    selected_cover();
    return;
  }
  if (depth) {
    depth--;
    snprintf(location, sizeof(location), "%s", history[depth].location);
    offset = history[depth].offset;
    selected_index = history[depth].selected_index;
    snprintf(folder_name, sizeof(folder_name), "%s", history_names[depth]);
    generation++;
    pending_browse = true;
    clear_cover();
    render();
  } else
    change_screen(CONTROLLER_SCREEN_MENU);
}
static void open_item(void) {
  if (pending_browse || (busy && busy_kind != IMAGE) || !page ||
      selected_index >= page->count)
    return;
  controller_library_item_t *item = &page->items[selected_index];
  if (*item->ref && !chooser) {
    chooser = true;
    render();
    return;
  }
  if (*item->location) {
    if (depth == HISTORY_MAX) {
      lv_label_set_text(subtitle, "Use Back before opening more folders.");
      return;
    }
    snprintf(history[depth].location, sizeof(history[depth].location), "%s",
             location);
    history[depth].offset = offset;
    history[depth].selected_index = selected_index;
    snprintf(history_names[depth], sizeof(history_names[depth]), "%s",
             folder_name);
    snprintf(folder_name, sizeof(folder_name), "%s", item->title);
    depth++;
    chooser = false;
    desired_absolute = -1;
    snprintf(location, sizeof(location), "%s", item->location);
    offset = selected_index = 0;
    generation++;
    pending_browse = true;
    clear_cover();
    render();
  } else if (*item->ref) {
    chooser = true;
    render();
  }
}
enum {
  ACTION_BACK,
  ACTION_PLAY,
  ACTION_QUEUE,
  ACTION_OPEN,
  ACTION_MENU,
  ACTION_LIBRARY,
  ACTION_ZONES,
  ACTION_SETTINGS,
  ACTION_HOME,
  ACTION_CONTROLS,
  ACTION_NEXT,
  ACTION_PREVIOUS,
  ACTION_RESET_CONFIRM,
  ACTION_REFRESH,
  ACTION_EDIT_WIFI,
  ACTION_EDIT_BRIDGE,
  ACTION_SETUP_WIFI,
  ACTION_AUTO_BRIDGE,
  ACTION_SAVE_ENTRY,
  ACTION_WIFI_DETAILS,
  ACTION_DEVICE_CONTROLS,
  ACTION_FORGET_NETWORK_CONFIRM,
  ACTION_USE_NETWORK = 1000,
  ACTION_FORGET_NETWORK = 1100,
  ACTION_ROW = 100
};
static void return_to_artwork(void) {
  if (screen == CONTROLLER_SCREEN_NOW_PLAYING)
    change_screen(CONTROLLER_SCREEN_ARTWORK);
}
static bool normalize_bridge(const char *input, char *out, size_t capacity) {
  while (isspace((unsigned char)*input)) input++;
  size_t length = strlen(input);
  while (length && isspace((unsigned char)input[length - 1])) length--;
  if (!length || length >= 128) return false;
  char value[128];
  memcpy(value, input, length); value[length] = 0;
  if (strncmp(value, "http://", 7) && strstr(value, "://")) return false;
  const char *host = !strncmp(value, "http://", 7) ? value + 7 : value;
  if (!*host || strchr(host, '@') || strchr(host, '?') || strchr(host, '#')) return false;
  char authority[128];
  snprintf(authority, sizeof(authority), "%s", host);
  char *slash = strchr(authority, '/');
  if (slash) { if (slash[1]) return false; *slash = 0; }
  if (!*authority || authority[0] == ':') return false;
  for (const char *c = authority; *c; c++)
    if (isspace((unsigned char)*c) || (unsigned char)*c < 32) return false;
  bool port = authority[0] == '[' ? strstr(authority, "]:") != NULL : strchr(authority, ':') != NULL;
  if (authority[0] == '[') {
    char *close = strchr(authority, ']');
    if (!close || close == authority + 1 || (close[1] && close[1] != ':')) return false;
  } else {
    const char *colon = strchr(authority, ':');
    if (colon && strchr(colon + 1, ':')) return false;
    for (const char *c = authority; *c && *c != ':'; c++)
      if (!isalnum((unsigned char)*c) && *c != '.' && *c != '-' && *c != '_') return false;
  }
  if (port) {
    const char *colon = strrchr(authority, ':');
    char *end;
    for (const char *c = colon + 1; *c; c++) if (!isdigit((unsigned char)*c)) return false;
    long number = strtol(colon + 1, &end, 10);
    if (!colon[1] || *end || number < 1 || number > 65535) return false;
  }
  int n = snprintf(out, capacity, "http://%s%s", authority, port ? "" : ":8088");
  return n > 0 && (size_t)n < capacity;
}
static bool search_folder(const controller_library_item_t *item) {
  if (item->requires_input) return true;
  if (!*item->location || *item->ref) return false;
  const char *p = item->title, *word = "search";
  while (*p && *word && tolower((unsigned char)*p) == *word) { p++; word++; }
  return !*p && !*word;
}
static void filter_library(controller_library_page_t *value) {
  int count = 0;
  for (int i = 0; i < value->count; i++)
    if (!search_folder(&value->items[i])) value->items[count++] = value->items[i];
  value->count = count;
}
static void refresh_connection(void) {
  char ip[32] = "";
  if (!wifi_mgr_get_ip(ip, sizeof(ip)) && !wifi_mgr_is_ap_mode()) {
    controller_config_wifi_snapshot_t networks;
    if (controller_config_wifi_snapshot(&networks) && networks.count) wifi_mgr_apply_wifi(&networks, true);
    else wifi_mgr_start_provisioning();
  }
  controller_connection_t connection;
  bridge_client_connection_snapshot(&connection);
  connection_observed_before = connection.observed_ms;
  connection_refresh_pending = true;
  connection_refresh_ms = esp_timer_get_time() / 1000;
  bridge_client_request_poll();
}
static void save_entry(void) {
  if (!entry) return;
  const char *value = lv_textarea_get_text(entry);
  if (edit_kind == 1) {
    if (!*value || strlen(value) > 32) { lv_label_set_text(subtitle, "Enter a network name"); return; }
    snprintf(edit_ssid, sizeof(edit_ssid), "%s", value);
    edit_kind = 2;
    render();
    return;
  }
  controller_config_write_result_t result = CONTROLLER_CONFIG_NOT_COMMITTED;
  if (edit_kind == 2) {
    if (strlen(value) == 64) {
      for (const char *c = value; *c; c++)
        if (!isxdigit((unsigned char)*c)) { lv_label_set_text(subtitle, "64-character keys must be hex"); return; }
    }
    if (*value && strlen(value) < 8) { lv_label_set_text(subtitle, "Password needs 8+ characters"); return; }
    result = controller_config_upsert_wifi(edit_ssid, value, true, NULL);
    if (result != CONTROLLER_CONFIG_NOT_COMMITTED) {
      controller_config_wifi_snapshot_t networks;
      if (controller_config_wifi_snapshot(&networks)) wifi_mgr_apply_wifi(&networks, true);
    }
  } else if (edit_kind == 3) {
    char base[128];
    if (!normalize_bridge(value, base, sizeof(base))) { lv_label_set_text(subtitle, "Enter a host or host:port"); return; }
    result = controller_config_set_endpoint(base, false, NULL);
    if (result != CONTROLLER_CONFIG_NOT_COMMITTED) bridge_client_request_poll();
  }
  if (result == CONTROLLER_CONFIG_NOT_COMMITTED) { lv_label_set_text(subtitle, "Could not save. Try again."); return; }
  edit_kind = 0;
  memset(edit_ssid, 0, sizeof(edit_ssid));
  snprintf(settings_notice, sizeof(settings_notice), "%s", result == CONTROLLER_CONFIG_COMMITTED_VERIFIED ? "Saved" : "Saved; storage verification failed");
  render();
}
static void keyboard_event(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_READY) save_entry();
  else if (lv_event_get_code(event) == LV_EVENT_CANCEL) { edit_kind = 0; memset(edit_ssid, 0, sizeof(edit_ssid)); render(); }
}
static void tap(lv_event_t *event) {
  int action = (int)(intptr_t)lv_event_get_user_data(event);
  if (action == ACTION_FORGET_NETWORK_CONFIRM && settings_detail == 7 && forget_index >= 0) {
    forget_index = -1;
    controller_config_write_result_t result = controller_config_remove_wifi_named(forget_ssid, NULL);
    memset(forget_ssid, 0, sizeof(forget_ssid));
    if (result != CONTROLLER_CONFIG_NOT_COMMITTED) {
      controller_config_wifi_snapshot_t networks;
      if (controller_config_wifi_snapshot(&networks)) wifi_mgr_apply_wifi(&networks, true);
    }
    settings_detail = 6;
    snprintf(settings_notice, sizeof(settings_notice), "%s", result == CONTROLLER_CONFIG_NOT_COMMITTED ? "Could not forget network" : "Network forgotten");
    render(); return;
  }
  if (action >= ACTION_FORGET_NETWORK && action < ACTION_FORGET_NETWORK + RK_MAX_WIFI && settings_detail == 6) {
    forget_index = action - ACTION_FORGET_NETWORK;
    snprintf(forget_ssid, sizeof(forget_ssid), "%s", displayed_networks[forget_index]);
    settings_detail = 7;
    render(); return;
  }
  if (action >= ACTION_USE_NETWORK && action < ACTION_USE_NETWORK + RK_MAX_WIFI && settings_detail == 6) {
    controller_config_write_result_t result = controller_config_promote_wifi_named(displayed_networks[action - ACTION_USE_NETWORK], NULL);
    if (result != CONTROLLER_CONFIG_NOT_COMMITTED) {
      controller_config_wifi_snapshot_t networks;
      if (controller_config_wifi_snapshot(&networks)) wifi_mgr_apply_wifi(&networks, true);
    }
    snprintf(settings_notice, sizeof(settings_notice), "%s", result == CONTROLLER_CONFIG_NOT_COMMITTED ? "Could not switch network" : "Connecting…");
    render(); return;
  }
  if (action == ACTION_DEVICE_CONTROLS) { platform_touch_show_device_controls(); return; }
  if (action == ACTION_WIFI_DETAILS) {
    change_screen(CONTROLLER_SCREEN_SETTINGS); settings_detail = 2; render(); return;
  }
  if (action == ACTION_REFRESH) { refresh_connection(); return; }
  if (action == ACTION_SAVE_ENTRY) { save_entry(); return; }
  if (action == ACTION_EDIT_WIFI || action == ACTION_EDIT_BRIDGE) {
    edit_kind = action == ACTION_EDIT_WIFI ? 1 : 3;
    settings_notice[0] = 0;
    render(); return;
  }
  if (action == ACTION_SETUP_WIFI) {
    snprintf(settings_notice, sizeof(settings_notice), "%s", wifi_mgr_start_provisioning() ? "Setup Wi-Fi started" : "Could not start setup Wi-Fi");
    render(); return;
  }
  if (action == ACTION_AUTO_BRIDGE) {
    controller_config_write_result_t result = controller_config_set_endpoint("", true, NULL);
    snprintf(settings_notice, sizeof(settings_notice), "%s", result == CONTROLLER_CONFIG_NOT_COMMITTED ? "Could not save" : "Finding music bridge…");
    bridge_client_request_poll(); render(); return;
  }
  if (action == ACTION_RESET_CONFIRM && screen == CONTROLLER_SCREEN_SETTINGS && settings_reset) {
    settings_reset = false;
    wifi_mgr_forget_wifi();
    settings_reset = true;
    snprintf(settings_notice, sizeof(settings_notice), "Reset did not complete. Try again.");
    render();
    return;
  }
  if (action == ACTION_MENU) {
    menu_index = 0;
    change_screen(CONTROLLER_SCREEN_MENU);
    return;
  }
  if (action == ACTION_HOME) {
    change_screen(CONTROLLER_SCREEN_ARTWORK);
    return;
  }
  if (action == ACTION_LIBRARY) {
    menu_index = 0;
    change_screen(CONTROLLER_SCREEN_LIBRARY);
    return;
  }
  if (action == ACTION_ZONES) {
    menu_index = 1;
    change_screen(CONTROLLER_SCREEN_ZONES);
    return;
  }
  if (action == ACTION_SETTINGS) {
    menu_index = 2;
    change_screen(CONTROLLER_SCREEN_SETTINGS);
    return;
  }
  if (action == ACTION_CONTROLS) {
    change_screen(CONTROLLER_SCREEN_NOW_PLAYING);
    return;
  }
  if (action == ACTION_BACK) {
    if (screen == CONTROLLER_SCREEN_SETTINGS && (settings_detail || edit_kind)) {
      if (edit_kind) { edit_kind = 0; memset(edit_ssid, 0, sizeof(edit_ssid)); }
      else if (settings_detail == 7) { settings_detail = 6; forget_index = -1; }
      else { settings_detail = 0; settings_about = settings_reset = false; }
      settings_notice[0] = 0;
      render();
      return;
    }
    if (screen == CONTROLLER_SCREEN_LIBRARY)
      back_action();
    else
      change_screen(CONTROLLER_SCREEN_MENU);
    return;
  }
  if (screen == CONTROLLER_SCREEN_ZONES && action >= ACTION_ROW) {
    int i = action - ACTION_ROW;
    if (i < zone_count) {
      bridge_zone_selection_result_t result = bridge_client_select_zone_value(zone_rows[i].id);
      if (!result.found || !result.persisted) {
        lv_label_set_text(subtitle, !result.found ? "Zone unavailable. Try again." : "Zone could not be saved.");
        return;
      }
      change_screen(CONTROLLER_SCREEN_ARTWORK);
    }
    return;
  }
  if (screen == CONTROLLER_SCREEN_SETTINGS && action >= ACTION_ROW) {
    setting_index = action - ACTION_ROW;
    if (setting_index == 0) {
      /* Brightness is a direct touch slider, not an undisclosed cycle. */
      return;
    } else {
      settings_detail = setting_index;
      settings_about = setting_index == 4;
      settings_reset = setting_index == 5;
      settings_notice[0] = 0;
      render();
    }
    return;
  }
  if (screen != CONTROLLER_SCREEN_LIBRARY)
    return;
  if (action == ACTION_NEXT) {
    move_item(page ? page->count - selected_index : 1);
    return;
  }
  if (action == ACTION_PREVIOUS) {
    move_item(-selected_index - 1);
    return;
  }
  if (pending_browse || (busy && busy_kind != IMAGE) || !page)
    return;
  if (!page->count) {
    generation++;
    pending_browse = true;
    render();
    return;
  }
  if (action >= ACTION_ROW) {
    selected_index = action - ACTION_ROW;
    open_item();
    return;
  }
  if (carousel && !chooser && lv_event_get_target(event) == primary)
    action = *page->items[selected_index].location ? ACTION_OPEN : ACTION_PLAY;
  if (carousel && !chooser && lv_event_get_target(event) == secondary)
    action = *page->items[selected_index].location ? ACTION_PLAY : ACTION_QUEUE;
  if (action == ACTION_OPEN) {
    open_item();
    return;
  }
  if ((action == ACTION_PLAY || action == ACTION_QUEUE) &&
      *page->items[selected_index].ref) {
    if (mutation_locked || pending_mutation >= 0)
      return;
    pending_mutation = action == ACTION_PLAY ? PLAY : QUEUE;
    pending_mutation_generation = generation;
    snprintf(pending_ref, sizeof(pending_ref), "%s",
             page->items[selected_index].ref);
    lv_label_set_text(subtitle, "Sending…");
    if (primary)
      lv_obj_add_state(primary, LV_STATE_DISABLED);
    if (secondary)
      lv_obj_add_state(secondary, LV_STATE_DISABLED);
  }
}
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int action) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, w, 48);
  lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
  lv_obj_set_style_pad_all(b, 4, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x252525), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x526573), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_t *text = lv_label_create(b);
  lv_obj_set_style_text_font(text, platform_touch_font_small(), 0);
  lv_obj_add_event_cb(b, tap, LV_EVENT_CLICKED, (void *)(intptr_t)action);
  return b;
}
static bool pointer_scrolling;
static void list_scroll(lv_event_t *event) {
  if (rebuilding || (carousel && screen == CONTROLLER_SCREEN_LIBRARY))
    return;
  if (lv_event_get_code(event) == LV_EVENT_SCROLL_BEGIN) {
    lv_indev_t *input = lv_indev_active();
    pointer_scrolling =
        input && lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER;
    return;
  }
  if (!pointer_scrolling || !scroller)
    return;
  pointer_scrolling = false;
  lv_area_t area;
  lv_obj_get_coords(scroller, &area);
  int center = (area.y1 + area.y2) / 2, nearest = -1, distance = 100000;
  for (int i = 0; i < BRIDGE_CLIENT_MAX_ZONES + 1; i++)
    if (rows[i]) {
      lv_area_t row;
      lv_obj_get_coords(rows[i], &row);
      int d = abs((row.y1 + row.y2) / 2 - center);
      if (d < distance) {
        distance = d;
        nearest = i;
      }
    }
  if (nearest < 0)
    return;
  if (screen == CONTROLLER_SCREEN_LIBRARY)
    selected_index = nearest;
  else if (screen == CONTROLLER_SCREEN_MENU)
    menu_index = nearest;
  else if (screen == CONTROLLER_SCREEN_SETTINGS)
    setting_index = nearest;
  else if (screen == CONTROLLER_SCREEN_ZONES)
    zone_index = nearest;
  for (int i = 0; i < BRIDGE_CLIENT_MAX_ZONES + 1; i++)
    if (rows[i])
      lv_obj_set_style_bg_color(
          rows[i], lv_color_hex(i == nearest ? 0x39434b : 0x181818), 0);
}
static void brightness_changed(lv_event_t *event) {
  brightness = lv_slider_get_value(lv_event_get_target(event));
  platform_touch_set_brightness(brightness);
  lv_label_set_text_fmt(lv_event_get_user_data(event), "%d%%", brightness);
  brightness_dirty = true;
  brightness_changed_ms = esp_timer_get_time() / 1000;
}
static void carousel_end(lv_event_t *e) {
  (void)e;
  if (rebuilding || !carousel || !page || !scroller)
    return;
  int center = lv_obj_get_scroll_x(scroller) + lv_obj_get_width(scroller) / 2;
  int nearest = selected_index, distance = 100000;
  for (int i = 0; i < page->count; i++)
    if (rows[i]) {
      int d =
          abs(lv_obj_get_x(rows[i]) + lv_obj_get_width(rows[i]) / 2 - center);
      if (d < distance) {
        distance = d;
        nearest = i;
      }
    }
  if (distance > 100) {
    if (primary)
      lv_obj_add_flag(primary, LV_OBJ_FLAG_HIDDEN);
    if (secondary)
      lv_obj_add_flag(secondary, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  selected_index = nearest;
  if (primary)
    lv_obj_remove_flag(primary, LV_OBJ_FLAG_HIDDEN);
  if (primary)
    button_text(primary, *page->items[nearest].location ? "Open" : "Play");
  if (secondary) {
    button_text(secondary, *page->items[nearest].location ? "Play" : "Queue");
    if (*page->items[nearest].ref)
      lv_obj_remove_flag(secondary, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(secondary, LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(counter, "");
}
static void ensure_overlay(void) {
  if (overlay)
    return;
  ui_remove_legacy_zone_touch_target();
  overlay = lv_obj_create(lv_screen_active());
  lv_obj_set_size(overlay, 360, 360);
  lv_obj_center(overlay);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x080808), 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  art_touch = lv_obj_create(lv_screen_active());
  lv_obj_set_size(art_touch, 360, 360);
  lv_obj_center(art_touch);
  lv_obj_set_style_bg_opa(art_touch, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(art_touch, 0, 0);
  lv_obj_remove_flag(art_touch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(art_touch, tap, LV_EVENT_CLICKED,
                      (void *)ACTION_CONTROLS);
  home_button = button(lv_screen_active(), 0, 294, 112, ACTION_MENU);
  button_text(home_button, "Menu");
}
static lv_obj_t *list_row(const char *name, const char *detail, int action,
                          int index) {
  lv_obj_t *b = button(scroller, 0, 0, 252, action);
  lv_obj_set_height(b, detail && *detail ? 76 : 56);
  lv_obj_t *t = lv_obj_get_child(b, 0);
  lv_obj_set_width(t, 228);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_label_set_text(t, name);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 8, 3);
  if (detail && *detail) {
    lv_obj_t *d;
    label(&d, b, 34, 228, platform_touch_font_small());
    lv_obj_set_style_text_color(d, lv_color_hex(0xb8c4cd), 0);
    lv_label_set_text(d, detail);
  }
  if (index >= 0 && index < BRIDGE_CLIENT_MAX_ZONES + 1)
    rows[index] = b;
  return b;
}
static void render(void) {
  ensure_overlay();
  rebuilding = true;
  /* Keep image descriptors stable while rebuilding widgets. */
  memset(cover_views, 0, sizeof(cover_views));
  memset(rows, 0, sizeof(rows));
  lv_obj_clean(overlay);
  scroller = NULL;
  entry = detail_label = NULL;
  if (screen == CONTROLLER_SCREEN_ARTWORK && recovery_active) {
    lv_obj_add_flag(home_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(art_touch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    label(&heading, overlay, 38, 260, platform_touch_font_small());
    lv_label_set_text(heading, recovery_title);
    label(&detail_label, overlay, 92, 254, platform_touch_font_small());
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(detail_label, 170);
    lv_label_set_text(detail_label, recovery_text);
    primary = button(overlay, -52, 278, 96, ACTION_SETTINGS);
    button_text(primary, "Settings");
    secondary = button(overlay, 52, 278, 96, recovery_choose_zone ? ACTION_ZONES : wifi_mgr_is_ap_mode() ? ACTION_WIFI_DETAILS : ACTION_REFRESH);
    button_text(secondary, recovery_choose_zone ? "Zones" : wifi_mgr_is_ap_mode() ? "Wi-Fi" : "Retry");
    rebuilding = false;
    return;
  }
  if (screen == CONTROLLER_SCREEN_ARTWORK ||
      screen == CONTROLLER_SCREEN_NOW_PLAYING) {
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    if (screen == CONTROLLER_SCREEN_ARTWORK) {
      lv_obj_remove_flag(art_touch, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(art_touch);
      lv_obj_add_flag(home_button, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(art_touch, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(home_button, LV_ALIGN_TOP_MID, 0, 34);
      lv_obj_remove_flag(home_button, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(home_button);
    }
    rebuilding = false;
    return;
  }
  lv_obj_add_flag(home_button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(art_touch, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
  label(&heading, overlay, 22, 200, platform_touch_font_small());
  label(&subtitle, overlay, 277, 256, platform_touch_font_small());
  label(&counter, overlay, 320, 100, platform_touch_font_small());
  back = button(overlay, -68, 53, 112, ACTION_BACK);
  button_text(back, "Back");
  lv_obj_t *home = button(overlay, 68, 53, 112, ACTION_HOME);
  button_text(home, "Home");
  scroller = lv_obj_create(overlay);
  lv_obj_set_size(scroller, 276, 170);
  lv_obj_align(scroller, LV_ALIGN_TOP_MID, 0, 106);
  lv_obj_set_style_bg_opa(scroller, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroller, 0, 0);
  lv_obj_set_style_pad_all(scroller, 4, 0);
  lv_obj_set_style_pad_row(scroller, 8, 0);
  lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
  lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_AUTO);
  primary = secondary = NULL;
  lv_label_set_text(subtitle, "Scroll to browse");
  lv_label_set_text(counter, "");
  if (screen == CONTROLLER_SCREEN_MENU) {
    lv_label_set_text(heading, "Music");
    lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(home, LV_ALIGN_TOP_MID, 0, 53);
    list_row("Library", NULL, ACTION_LIBRARY, 0);
    char current[64] = "", name[100] = "Pick a zone";
    bridge_client_get_current_zone_id(current, sizeof(current));
    bridge_zone_t zs[BRIDGE_CLIENT_MAX_ZONES];
    int n = bridge_client_get_zones(zs, BRIDGE_CLIENT_MAX_ZONES);
    for (int i = 0; i < n; i++)
      if (!strcmp(zs[i].id, current))
        snprintf(name, sizeof(name), "Zone: %s", zs[i].name);
    list_row(name, NULL, ACTION_ZONES, 1);
    list_row("Settings", NULL, ACTION_SETTINGS, 2);

    focus_row(menu_index);
  } else if (screen == CONTROLLER_SCREEN_ZONES) {
    lv_label_set_text(heading, "Pick a zone");
    for (int i = 0; i < zone_count; i++)
      list_row(zone_rows[i].name, NULL, ACTION_ROW + i, i);
    if (!zone_count)
      lv_label_set_text(subtitle, "No zones. Check Settings.");
    focus_row(zone_index);
  } else if (screen == CONTROLLER_SCREEN_SETTINGS) {
    lv_label_set_text(heading, "Settings");
    if (edit_kind) {
      lv_obj_add_flag(scroller, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(heading, edit_kind == 1 ? "Network name" : edit_kind == 2 ? "Wi-Fi password" : "Music bridge");
      entry = lv_textarea_create(overlay);
      lv_obj_set_size(entry, 252, 42);
      lv_obj_align(entry, LV_ALIGN_TOP_MID, 0, 105);
      lv_textarea_set_one_line(entry, true);
      lv_textarea_set_max_length(entry, edit_kind == 1 ? 32 : edit_kind == 2 ? 64 : 127);
      lv_textarea_set_password_mode(entry, edit_kind == 2);
      if (edit_kind == 1) {
        controller_config_snapshot_t cfg = {0}; controller_config_snapshot(&cfg);
        lv_textarea_set_text(entry, cfg.value.ssid);
      } else if (edit_kind == 3) {
        char base[128] = ""; bridge_client_get_bridge_url(base, sizeof(base));
        lv_textarea_set_text(entry, base);
      }
      lv_obj_t *keyboard = lv_keyboard_create(overlay);
      lv_obj_set_size(keyboard, 276, 99);
      lv_obj_align(keyboard, LV_ALIGN_TOP_MID, 0, 169);
      lv_obj_set_style_bg_color(entry, lv_color_hex(0x151515), 0);
      lv_obj_set_style_text_color(entry, lv_color_hex(0xeeeeee), 0);
      lv_obj_set_style_border_color(entry, lv_color_hex(0x526573), 0);
      lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x080808), 0);
      lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x252525), LV_PART_ITEMS);
      lv_obj_set_style_text_color(keyboard, lv_color_hex(0xeeeeee), LV_PART_ITEMS);
      lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x526573), LV_PART_ITEMS | LV_STATE_PRESSED);
      lv_keyboard_set_textarea(keyboard, entry);
      lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_READY, NULL);
      lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
      primary = button(overlay, 0, 278, 120, ACTION_SAVE_ENTRY);
      button_text(primary, edit_kind == 1 ? "Next" : "Save");
      lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 148);
      lv_obj_set_style_text_font(subtitle, LV_FONT_DEFAULT, 0);
      lv_label_set_text(subtitle, edit_kind == 2 ? "Empty for an open network" : edit_kind == 3 ? "Host; default port 8088" : "Wi-Fi name (SSID)");
      rebuilding = false;
      return;
    }
    if (settings_detail) {
      static const char *titles[] = {"", "Connection", "Wi-Fi", "Music bridge", "About", "Factory reset?", "Saved networks", "Forget network?"};
      lv_label_set_text(heading, titles[settings_detail]);
      lv_obj_set_height(scroller, 156);
      detail_label = lv_label_create(scroller);
      lv_obj_set_width(detail_label, 254);
      lv_obj_set_style_text_font(detail_label, platform_touch_font_small(), 0);
      lv_obj_set_style_text_color(detail_label, lv_color_hex(0xeeeeee), 0);
      lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
      char text[900], ip[32] = "";
      controller_config_snapshot_t cfg = {0}; controller_config_snapshot(&cfg);
      bool has_ip = wifi_mgr_get_ip(ip, sizeof(ip));
      if (settings_detail == 1) {
        char summary[100], details[600];
        bridge_client_connection_status(summary, sizeof(summary), details, sizeof(details));
        snprintf(text, sizeof(text), "%s\n%s", summary, details);
        primary = button(overlay, 0, 278, 120, ACTION_REFRESH); button_text(primary, "Refresh");
      } else if (settings_detail == 2) {
        char setup[180];
        if (wifi_mgr_is_ap_mode()) snprintf(setup, sizeof(setup), "Join %s\nOpen 192.168.4.1", platform_provisioning_ssid());
        else snprintf(setup, sizeof(setup), "Use Change below, or Setup for phone configuration.");
        snprintf(text, sizeof(text), "Network: %s\n%s\n%s", *cfg.value.ssid ? cfg.value.ssid : "Not configured", has_ip ? ip : "No network address", setup);
        primary = button(overlay, -52, 278, 96, ACTION_EDIT_WIFI); button_text(primary, "Change");
        secondary = button(overlay, 52, 278, 96, ACTION_SETUP_WIFI); button_text(secondary, "Setup");
      } else if (settings_detail == 3) {
        char base[128] = "", summary[100], details[400];
        bridge_client_get_bridge_url(base, sizeof(base));
        bridge_client_connection_status(summary, sizeof(summary), details, sizeof(details));
        snprintf(text, sizeof(text), "%s\n%s\n%s\n%s", *base ? base : "Automatic discovery", cfg.value.bridge_from_mdns ? "Discovered on your network" : "Manual address", summary, details);
        primary = button(overlay, -52, 278, 96, ACTION_EDIT_BRIDGE); button_text(primary, "Edit");
        secondary = button(overlay, 52, 278, 96, ACTION_AUTO_BRIDGE); button_text(secondary, "Auto");
      } else if (settings_detail == 4) {
        snprintf(text, sizeof(text), "%s\n%s\nFirmware %s\n%s\n%s\nhiphi.audio", platform_product_name(), *cfg.value.knob_name ? cfg.value.knob_name : wifi_mgr_get_hostname(), portal_brand_firmware_version(), wifi_mgr_get_hostname(), has_ip ? ip : "No network address");
      } else if (settings_detail == 6) {
        lv_label_set_text(detail_label, cfg.value.wifi_count ? "Tap Use to connect, or Forget." : "No saved networks");
        memset(displayed_networks, 0, sizeof(displayed_networks));
        for (int i = 0; i < cfg.value.wifi_count && i < RK_MAX_WIFI; i++) {
          snprintf(displayed_networks[i], sizeof(displayed_networks[i]), "%s", cfg.value.wifi[i].ssid);
          char name[80];
          snprintf(name, sizeof(name), "Use: %s", cfg.value.wifi[i].ssid);
          list_row(name, NULL, ACTION_USE_NETWORK + i, i * 2);
          snprintf(name, sizeof(name), "Forget: %s", cfg.value.wifi[i].ssid);
          list_row(name, NULL, ACTION_FORGET_NETWORK + i, i * 2 + 1);
        }
        lv_obj_align(scroller, LV_ALIGN_TOP_MID, 0, 120);
        lv_obj_set_height(scroller, 180);
        lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 103);
        lv_obj_set_style_text_font(subtitle, LV_FONT_DEFAULT, 0);
        lv_label_set_text(subtitle, settings_notice);
        rebuilding = false; return;
      } else if (settings_detail == 7) {
        snprintf(text, sizeof(text), "Forget %s?\nOther saved networks remain.", *forget_ssid ? forget_ssid : "this network");
        primary = button(overlay, 0, 278, 152, ACTION_FORGET_NETWORK_CONFIRM); button_text(primary, "Forget");
      } else {
        snprintf(text, sizeof(text), "Erase Wi-Fi and all saved settings?\n%s will restart in setup mode.", platform_product_name());
        primary = button(overlay, 0, 278, 152, ACTION_RESET_CONFIRM); button_text(primary, "Reset device");
      }
      if (cfg.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT || cfg.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY)
        strncat(text, "\nSettings storage unavailable or unverified", sizeof(text) - strlen(text) - 1);
      lv_label_set_text(detail_label, text);
      lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 80);
      lv_obj_set_style_text_font(subtitle, LV_FONT_DEFAULT, 0);
      lv_obj_set_style_bg_color(subtitle, lv_color_hex(0x080808), 0);
      lv_obj_set_style_bg_opa(subtitle, LV_OPA_COVER, 0);
      lv_obj_align(scroller, LV_ALIGN_TOP_MID, 0, 120);
      lv_obj_set_height(scroller, 145);
      lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 103);
      lv_label_set_text(subtitle, settings_notice);
      rebuilding = false;
      return;
    }
    char detail[160];
    snprintf(detail, sizeof(detail), "%d%%", brightness);
    lv_obj_t *brightness_row = list_row("Brightness", detail, ACTION_ROW, 0);
    lv_obj_set_height(brightness_row, 100);
    lv_obj_t *slider = lv_slider_create(brightness_row);
    lv_obj_set_size(slider, 210, 12);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 70);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xa9c6dc),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xd7e8f4), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_changed, LV_EVENT_VALUE_CHANGED,
                        lv_obj_get_child(brightness_row, 1));
    char summary[100], status[200];
    bridge_client_connection_status(summary, sizeof(summary), status,
                                    sizeof(status));
    list_row("Connection", summary, ACTION_ROW + 1, 1);
    controller_config_snapshot_t cfg;
    controller_config_snapshot(&cfg);
    list_row("Wi-Fi", cfg.value.ssid, ACTION_ROW + 2, 2);
    char base[128] = "";
    bridge_client_get_bridge_url(base, sizeof(base));
    list_row("Music bridge", base, ACTION_ROW + 3, 3);
    list_row("About", "Device and firmware", ACTION_ROW + 4, 4);
    list_row("Factory reset", "Erase saved settings", ACTION_ROW + 5, 5);
    list_row("Saved networks", "Switch or forget Wi-Fi", ACTION_ROW + 6, 6);
    if (platform_touch_has_device_controls())
      list_row("Device controls", "Updates and BLE status", ACTION_DEVICE_CONTROLS, 7);
    lv_label_set_text(subtitle, "Tap connection to refresh");
    focus_row(setting_index);
  } else {
    lv_label_set_text(heading, chooser ? "Actions" : folder_name);
    if (pending_browse || (busy && busy_kind != IMAGE)) {
      lv_label_set_text(subtitle, busy && busy_kind != BROWSE
                                      ? "Sending playback…"
                                      : "Loading library…");
    } else if (!page || !page->count) {
      list_row("Retry library", NULL, ACTION_OPEN, 0);
      lv_obj_t *message = lv_label_create(scroller);
      lv_obj_set_width(message, 252);
      lv_obj_set_style_text_font(message, platform_touch_font_small(), 0);
      lv_obj_set_style_text_color(message, lv_color_hex(0xeeeeee), 0);
      lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
      lv_label_set_text(message, page && *page->notice ? page->notice : "No items");
      lv_label_set_text(subtitle, "");
    } else if (chooser) {
      lv_obj_set_height(scroller, 120);
      lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 235);
      list_row(page->items[selected_index].title,
               *page->items[selected_index].location ? "Open collection" : NULL,
               ACTION_OPEN, -1);
      primary = button(overlay, -52, 278, 96, ACTION_PLAY);
      button_text(primary, "Play");
      secondary = button(overlay, 52, 278, 96, ACTION_QUEUE);
      button_text(secondary, "Queue");
      lv_label_set_text(subtitle, mutation_locked
                                      ? "Check now playing before retrying"
                                      : "Choose playback action");
    } else {
      /* A stable text layout avoids asynchronous cover changes and keeps
       * large collections legible on the round display. */
      carousel = false;
      if (carousel) {
        lv_obj_set_size(scroller, 300, 175);
        lv_obj_align(scroller, LV_ALIGN_TOP_MID, 0, 101);
        lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_ROW);
        lv_obj_set_scroll_dir(scroller, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(scroller, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_style_pad_left(scroller, 50, 0);
        lv_obj_set_style_pad_right(scroller, 50, 0);
        if (offset > 0) {
          lv_obj_t *b = button(scroller, 0, 0, 192, ACTION_PREVIOUS);
          lv_obj_set_height(b, 168);
          lv_obj_add_flag(b, LV_OBJ_FLAG_SNAPPABLE);
          button_text(b, "Previous page");
        }
        for (int i = 0; i < page->count; i++) {
          lv_obj_t *card =
              list_row(page->items[i].title, NULL, ACTION_ROW + i, i);
          lv_obj_set_size(card, 192, 168);
          lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE);
          lv_obj_t *caption = lv_obj_get_child(card, 0);
          lv_obj_set_width(caption, 176);
          lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, 0);
          cover_views[i] = lv_image_create(card);
          lv_obj_align(cover_views[i], LV_ALIGN_TOP_MID, 0, 0);
          lv_obj_remove_flag(cover_views[i], LV_OBJ_FLAG_CLICKABLE);
          if (covers[i].pixels)
            lv_image_set_src(cover_views[i], &covers[i].descriptor);
          else {
            lv_obj_t *placeholder;
            label(&placeholder, card, 55, 176, platform_touch_font_small());
            lv_label_set_text(placeholder,
                              *page->items[i].image && !covers[i].failed
                                  ? "Loading cover…"
                              : *page->items[i].location ? "Collection"
                                                         : "No cover");
          }
        }
        lv_obj_update_layout(scroller);
        lv_obj_scroll_to_view(rows[selected_index], LV_ANIM_OFF);
        lv_obj_add_event_cb(scroller, carousel_end, LV_EVENT_SCROLL_END, NULL);
        primary = button(overlay, -52, 278, 96,
                         *page->items[selected_index].location ? ACTION_OPEN
                                                               : ACTION_PLAY);
        button_text(primary,
                    *page->items[selected_index].location ? "Open" : "Play");
        secondary = button(
            overlay, 52, 278, 96,
            *page->items[selected_index].location ? ACTION_PLAY : ACTION_QUEUE);
        button_text(secondary,
                    *page->items[selected_index].location ? "Play" : "Queue");
        if (!*page->items[selected_index].ref)
          lv_obj_add_flag(secondary, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(subtitle, "");
        lv_label_set_text(counter, "");
        lv_obj_align(counter, LV_ALIGN_TOP_MID, 0, 78);
        /* Page boundaries are explicit, never wrap through unrelated content.
         */
        if (page->next_offset >= 0) {
          lv_obj_t *b = button(scroller, 0, 0, 192, ACTION_NEXT);
          lv_obj_set_height(b, 168);
          lv_obj_add_flag(b, LV_OBJ_FLAG_SNAPPABLE);
          button_text(b, "More albums");
        }
      } else {
        for (int i = 0; i < page->count; i++)
          list_row(page->items[i].title, page->items[i].subtitle,
                   ACTION_ROW + i, i);
        if (offset > 0)
          list_row("Previous page", NULL, ACTION_PREVIOUS, -1);
        if (page->next_offset >= 0)
          list_row("More", NULL, ACTION_NEXT, -1);
        focus_row(selected_index);
      }
    }
  }
  lv_obj_add_event_cb(scroller, list_scroll, LV_EVENT_SCROLL_BEGIN, NULL);
  lv_obj_add_event_cb(scroller, list_scroll, LV_EVENT_SCROLL_END, NULL);
  rebuilding = false;
}
static bool submit(int kind, const char *ref, const char *image,
                   int image_index) {
  if (busy || !requests)
    return false;
  if ((kind == PLAY || kind == QUEUE) && mutation_locked)
    return false;
  request_t *r =
      heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!r)
    return false;
  r->generation = kind == IMAGE ? generation : ++generation;
  r->item_index = image_index;
  r->kind = kind;
  r->offset = offset;
  if (!bridge_client_get_request_base(r->endpoint, sizeof(r->endpoint)) ||
      !bridge_client_get_current_zone_id(r->zone, sizeof(r->zone))) {
    free(r);
    if (page)
      snprintf(page->notice, sizeof(page->notice),
               "Pick a zone and connect first.");
    if (page && kind == BROWSE)
      page->count = 0;
    render();
    return false;
  }
  snprintf(r->location, sizeof(r->location), "%s", location);
  if (ref)
    snprintf(r->ref, sizeof(r->ref), "%s", ref);
  if (image)
    snprintf(r->image, sizeof(r->image), "%s", image);
  busy = xQueueSend(requests, r, 0) == pdTRUE;
  if (busy)
    busy_kind = kind;
  if (busy && (kind == PLAY || kind == QUEUE)) {
    mutation_locked = true;
    mutation_sent_ms = esp_timer_get_time() / 1000;
  }
  free(r);
  if (kind != IMAGE) {
    clear_cover();
    render();
  }
  return busy;
}
void controller_touch_navigation_process(void) {
  if (!page) {
    page =
        heap_caps_calloc(1, sizeof(*page), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!page)
      return;
    page->next_offset = -1;
    brightness = platform_touch_load_brightness();
    platform_touch_set_brightness(brightness);
    ui_set_controls_visible(false);
    ui_set_artwork_only(true);
    render();
    requests = xQueueCreate(1, sizeof(request_t));
    results = xQueueCreate(1, sizeof(result_t *));
    if (!requests || !results ||
        xTaskCreate(worker, "touch_library", 12288, NULL, 3, NULL) != pdPASS) {
      snprintf(page->notice, sizeof(page->notice),
               "Library memory unavailable");
      requests = NULL;
    }
  }
  char base[128] = "", selected[64] = "";
  bridge_client_get_request_base(base, sizeof(base));
  bridge_client_get_current_zone_id(selected, sizeof(selected));
  if (strcmp(base, endpoint) || strcmp(selected, zone)) {
    snprintf(endpoint, sizeof(endpoint), "%s", base);
    snprintf(zone, sizeof(zone), "%s", selected);
    reset_library();
    clear_cover();
    if (screen == CONTROLLER_SCREEN_LIBRARY) {
      pending_browse = true;
      render();
    }
  }
  result_t *result;
  if (results && xQueueReceive(results, &result, 0) == pdTRUE) {
    busy = false;
    if (result && result->request.generation == generation &&
        request_current(&result->request)) {
      if (result->request.kind == IMAGE) {
        int image_index = result->request.item_index;
        if (!result->ok && image_index >= 0 && image_index < page->count) {
          covers[image_index].failed = true;
          if (cover_views[image_index]) {
            lv_obj_t *card = lv_obj_get_parent(cover_views[image_index]);
            if (lv_obj_get_child_count(card) > 2)
              lv_label_set_text(lv_obj_get_child(card, 2), "No cover");
          }
        }
        if (result->ok && screen == CONTROLLER_SCREEN_LIBRARY) {
          int i = result->request.item_index;
          if (i >= 0 && i < page->count) {
            covers[i].pixels = result->pixels;
            result->pixels = NULL;
            lv_image_dsc_t *d = &covers[i].descriptor;
            d->header.magic = LV_IMAGE_HEADER_MAGIC;
            d->header.cf = LV_COLOR_FORMAT_RGB565;
            d->header.w = COVER_SIZE;
            d->header.h = COVER_SIZE;
            d->header.stride = COVER_SIZE * 2;
            d->data_size = COVER_BYTES;
            d->data = (uint8_t *)covers[i].pixels;
            if (cover_views[i]) {
              lv_image_set_src(cover_views[i], d);
              lv_obj_t *card = lv_obj_get_parent(cover_views[i]);
              if (lv_obj_get_child_count(card) > 2)
                lv_obj_add_flag(lv_obj_get_child(card, 2), LV_OBJ_FLAG_HIDDEN);
            }
          }
        }
      } else if (result->request.kind == BROWSE) {
        *page = result->page;
        filter_library(page);
        if (result->ok && !page->count && page->next_offset > offset) {
          offset = page->next_offset;
          selected_index = 0;
          pending_browse = true;
        }
        if (desired_absolute >= 0 && result->ok && page->count) {
          if (desired_absolute >= offset + page->count &&
              page->next_offset >= 0) {
            offset = page->next_offset;
            selected_index = 0;
            pending_browse = true;
          } else if (desired_absolute < offset && offset > 0) {
            offset = offset > CONTROLLER_LIBRARY_PAGE_SIZE
                         ? offset - CONTROLLER_LIBRARY_PAGE_SIZE
                         : 0;
            selected_index = 0;
            pending_browse = true;
          } else {
            selected_index = desired_absolute - offset;
            if (selected_index < 0)
              selected_index = 0;
            desired_absolute = -1;
          }
        } else
          desired_absolute = -1;
        if (selected_index >= page->count)
          selected_index = page->count ? page->count - 1 : 0;
        render();
        if (result->ok)
          selected_cover();
      } else {
        bridge_client_request_poll();
        if (result->ok) {
          reset_library();
          change_screen(CONTROLLER_SCREEN_NOW_PLAYING);
        } else {
          page->count = 0;
          snprintf(page->notice, sizeof(page->notice), "%s",
                   result->page.notice);
          render();
        }
      }
    } else if (screen == CONTROLLER_SCREEN_LIBRARY && !pending_browse &&
               !result) {
      page->count = 0;
      snprintf(page->notice, sizeof(page->notice),
               "Not enough memory. Tap Retry.");
      render();
    }
    if (result) {
      platform_http_free(result->pixels);
      free(result);
    }
  }
  /* No automatic mutation replay. A later fresh bridge poll permits another
   * deliberate action. */
  if (mutation_locked && !busy) {
    controller_connection_t c;
    bridge_client_connection_snapshot(&c);
    if (controller_connection_ready(&c) &&
        c.last_success_ms > (uint64_t)mutation_sent_ms)
      mutation_locked = false;
  }
  if (screen == CONTROLLER_SCREEN_SETTINGS &&
      esp_timer_get_time() / 1000 - connection_status_ms >= 1000) {
    connection_status_ms = esp_timer_get_time() / 1000;
    if (rows[1]) {
      controller_connection_t c;
      bridge_client_connection_snapshot(&c);
      char summary[100], detail[200];
      bridge_client_connection_status(summary, sizeof(summary), detail,
                                      sizeof(detail));
      if (connection_refresh_pending) {
        if (c.observed_ms > connection_observed_before)
          connection_refresh_pending = false;
        else if (connection_status_ms - connection_refresh_ms < 5000)
          snprintf(summary, sizeof(summary), "Checking…");
        else {
          connection_refresh_pending = false;
          snprintf(summary, sizeof(summary), "Refresh timed out");
        }
      }
      lv_obj_t *text = lv_obj_get_child(rows[1], 1);
      if (strcmp(lv_label_get_text(text), summary))
        lv_label_set_text(text, summary);
    }
  }
  int64_t clock_ms = esp_timer_get_time() / 1000;
  if (brightness_dirty && clock_ms - brightness_changed_ms >= 700) {
    brightness_dirty = false;
    bool saved = platform_touch_save_brightness(brightness);
    snprintf(settings_notice, sizeof(settings_notice), "%s", saved ? "Brightness saved" : "Brightness could not be saved");
    if (screen == CONTROLLER_SCREEN_SETTINGS && !settings_detail && !edit_kind)
      lv_label_set_text(subtitle, settings_notice);
  }
  if (clock_ms - recovery_check_ms >= 1000) {
    recovery_check_ms = clock_ms;
    controller_config_snapshot_t cfg = {0}; controller_config_snapshot(&cfg);
    controller_connection_t connection; bridge_client_connection_snapshot(&connection);
    char title[64] = "", text[600] = "", ip[32] = "";
    bool has_ip = wifi_mgr_get_ip(ip, sizeof(ip));
    bool recovery = true;
    bool choose_zone = false;
    if (wifi_mgr_is_ap_mode()) {
      snprintf(title, sizeof(title), "Set up Wi-Fi");
      snprintf(text, sizeof(text), "On your phone:\nJoin %s\nOpen 192.168.4.1\nOr use Wi-Fi in Settings.", platform_provisioning_ssid());
    } else if (!has_ip) {
      snprintf(title, sizeof(title), "Connecting to Wi-Fi");
      const char *error = wifi_mgr_get_last_error();
      snprintf(text, sizeof(text), "%s\nAttempt %d of %d\nUse Wi-Fi in Settings to change networks.", error && *error ? error : "Waiting for network", wifi_mgr_get_retry_count(), wifi_mgr_get_retry_max());
    } else if (cfg.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT || cfg.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY) {
      snprintf(title, sizeof(title), "Settings storage warning");
      snprintf(text, sizeof(text), "Changes may not survive a restart.\nSee Connection in Settings.");
    } else if (!controller_connection_ready(&connection)) {
      char summary[100];
      snprintf(title, sizeof(title), "Music connection");
      controller_connection_summary(&connection, summary, sizeof(summary));
      choose_zone = !connection.offline && !connection.stale && !connection.ambiguous &&
          connection.reachable && connection.zones_current && connection.zone_count > 0 &&
          !connection.selected_zone_available;
      if (choose_zone) {
        snprintf(title, sizeof(title), "Choose a zone");
        snprintf(text, sizeof(text), "Your bridge has %d zones.\nChoose one to control.", connection.zone_count);
      } else {
        snprintf(text, sizeof(text), "%s\n\nDetails in Settings.", summary);
      }
    } else recovery = false;
    bool changed = recovery_active != recovery || recovery_choose_zone != choose_zone || strcmp(recovery_title, title) || strcmp(recovery_text, text);
    recovery_active = recovery;
    recovery_choose_zone = choose_zone;
    snprintf(recovery_title, sizeof(recovery_title), "%s", title);
    snprintf(recovery_text, sizeof(recovery_text), "%s", text);
    if (changed && screen == CONTROLLER_SCREEN_ARTWORK) render();
    if (connection_refresh_pending) {
      if (connection.observed_ms > connection_observed_before) connection_refresh_pending = false;
      else if (clock_ms - connection_refresh_ms >= 5000) {
        connection_refresh_pending = false;
        snprintf(settings_notice, sizeof(settings_notice), "Refresh timed out");
      }
    }
    if (screen == CONTROLLER_SCREEN_SETTINGS && settings_detail == 1 && !edit_kind && detail_label) {
      char summary[100], details[600], output[750];
      bridge_client_connection_status(summary, sizeof(summary), details, sizeof(details));
      snprintf(output, sizeof(output), "%s\n%s%s", summary, details, cfg.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT || cfg.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY ? "\nSettings storage unavailable or unverified" : "");
      if (strcmp(lv_label_get_text(detail_label), output)) lv_label_set_text(detail_label, output);
      lv_label_set_text(subtitle, connection_refresh_pending ? "Checking…" : settings_notice);
    }
  }
  if (screen == CONTROLLER_SCREEN_LIBRARY && !busy) {
    if (pending_mutation >= 0) {
      int kind = pending_mutation;
      pending_mutation = -1;
      if (pending_mutation_generation == generation)
        submit(kind, pending_ref, NULL, -1);
    } else if (pending_browse) {
      pending_browse = false;
      submit(BROWSE, NULL, NULL, -1);
    } else
      selected_cover();
  }
}
bool controller_touch_navigation_encoder(int delta) {
  if (platform_touch_device_controls_visible()) { platform_touch_scroll_device_controls(delta); return true; }
  if (screen == CONTROLLER_SCREEN_ARTWORK && recovery_active) return true;
  if (screen == CONTROLLER_SCREEN_MENU) {
    menu_index += delta;
    if (menu_index < 0)
      menu_index = 0;
    if (menu_index > 2)
      menu_index = 2;
    focus_row(menu_index);
    return true;
  }
  if (screen == CONTROLLER_SCREEN_ZONES || ui_is_zone_picker_visible()) {
    if (ui_is_zone_picker_visible()) {
      while (delta) {
        ui_zone_picker_scroll(delta > 0 ? 1 : -1);
        delta += delta > 0 ? -1 : 1;
      }
    } else {
      zone_index += delta;
      if (zone_index < 0)
        zone_index = 0;
      if (zone_index >= zone_count)
        zone_index = zone_count - 1;
      focus_row(zone_index);
    }
    return true;
  }
  if (screen == CONTROLLER_SCREEN_LIBRARY) {
    move_item(delta);
    return true;
  }
  if (screen == CONTROLLER_SCREEN_SETTINGS) {
    if (edit_kind) return true;
    if (settings_detail) {
      if (scroller) lv_obj_scroll_by(scroller, 0, -delta * 45, LV_ANIM_ON);
      return true;
    }
    setting_index += delta;
    if (setting_index < 0)
      setting_index = 0;
    if (setting_index > (platform_touch_has_device_controls() ? 7 : 6))
      setting_index = platform_touch_has_device_controls() ? 7 : 6;
    focus_row(setting_index);
    return true;
  }
  if (delta && screen == CONTROLLER_SCREEN_ARTWORK)
    change_screen(CONTROLLER_SCREEN_NOW_PLAYING);
  return !controller_navigation_encoder_uses_volume(screen);
}
void controller_touch_navigation_settings(void) {
  change_screen(CONTROLLER_SCREEN_SETTINGS);
}
bool controller_touch_navigation_settings_visible(void) {
  return screen == CONTROLLER_SCREEN_SETTINGS;
}

void controller_touch_navigation_close_settings(void) {
  if (screen == CONTROLLER_SCREEN_SETTINGS)
    change_screen(CONTROLLER_SCREEN_NOW_PLAYING);
}

void controller_touch_navigation_show_artwork(void) {
  if (screen != CONTROLLER_SCREEN_ARTWORK) change_screen(CONTROLLER_SCREEN_ARTWORK);
}
void controller_touch_navigation_restore_visibility(void) {
  ui_set_controls_visible(screen == CONTROLLER_SCREEN_NOW_PLAYING);
  ui_set_artwork_only(screen == CONTROLLER_SCREEN_ARTWORK);
}
