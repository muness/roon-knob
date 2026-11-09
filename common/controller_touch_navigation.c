/* Shared 360px round touch navigation; hardware and power policy stay target-owned. */
#include "controller_touch_navigation.h"
#include "controller_device_settings.h"
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
#include <limits.h>
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

enum { BROWSE, PLAY, QUEUE, IMAGE, SHORTCUTS_FETCH, SHORTCUTS_PLAY, PREFETCH_NEXT, PREFETCH_PREVIOUS };
#define HISTORY_MAX 8
#define COVER_SIZE 48
#define LIBRARY_PREFETCH_AHEAD (CONTROLLER_LIBRARY_PAGE_SIZE / 2)
#define COVER_BYTES (COVER_SIZE * COVER_SIZE * 2)
typedef struct {
  bridge_shortcuts_context_t shortcuts_context;
  unsigned generation;
  int kind, offset, item_index;
  char endpoint[128], zone[64], location[513], ref[513], image[600];
} request_t;
typedef struct {
  request_t request;
  controller_library_page_t page;
  controller_shortcuts_page_t shortcuts;
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
static controller_shortcuts_page_t *shortcuts;
static bridge_shortcuts_context_t shortcuts_context;
static int shortcut_index, shortcut_offset;
static bool pending_shortcuts;
static char endpoint[128], zone[64], location[513];
static struct {
  char location[513];
  int offset, selected_index;
  char collection_ref[CONTROLLER_LIBRARY_HANDLE_SIZE];
} history[HISTORY_MAX];
static int depth, setting_index, menu_index, zone_index, zone_count;
static int desired_absolute = -1;
static bool connection_refresh_pending;
static uint64_t connection_observed_before;
static int64_t connection_refresh_ms, connection_status_ms;
static bridge_zone_t zone_rows[BRIDGE_CLIENT_MAX_ZONES];
static char folder_name[301] = "Library";
static bool pending_browse, carousel, rebuilding;
static lv_obj_t *scroller, *home_button, *art_touch;
#define TOUCH_ROW_COUNT ((CONTROLLER_LIBRARY_CAPACITY > BRIDGE_CLIENT_MAX_ZONES + 1) ? CONTROLLER_LIBRARY_CAPACITY : BRIDGE_CLIENT_MAX_ZONES + 1)
static lv_obj_t *rows[TOUCH_ROW_COUNT];
static lv_obj_t *cover_views[CONTROLLER_LIBRARY_CAPACITY];
static char history_names[HISTORY_MAX][301];
static lv_obj_t *overlay, *heading, *subtitle, *counter, *back, *primary,
    *secondary;
static struct {
  lv_image_dsc_t descriptor;
  char *pixels;
  bool attempted, failed;
} covers[CONTROLLER_LIBRARY_CAPACITY];
static controller_library_page_t *cached_next, *cached_previous;
static bool next_cached, previous_cached, prefetch_failed, prefetch_back;
static int next_cached_offset, previous_cached_offset;
static int leading_count, trailing_offset = -1;
static bool waiting_selection;
static int wanted_index;
static bool pointer_scrolling;
static int scroll_start_y;
#define LIBRARY_VISIBLE_ROWS 9
static struct { lv_obj_t *row; int index; unsigned epoch; } visible_rows[LIBRARY_VISIBLE_ROWS];
static lv_obj_t *top_spacer, *bottom_spacer;
static bool virtual_library, updating_visible;
static unsigned library_data_epoch;
static int visible_first = -1, visible_count = -1;
static lv_obj_t *pressed_library_row;
static int pressed_library_index;
static unsigned pressed_library_epoch, pressed_library_generation;
static void library_virtual_refresh(void);
static int library_item_y(int index);
static int library_row_height(int index);

static char collection_ref[CONTROLLER_LIBRARY_HANDLE_SIZE];
static void library_cache_invalidate(void) {
  next_cached = previous_cached = prefetch_failed = prefetch_back = waiting_selection = false;
  leading_count = 0;
  trailing_offset = -1;
}
static bool library_busy(void) {
  return busy && busy_kind != IMAGE && busy_kind != PREFETCH_NEXT && busy_kind != PREFETCH_PREVIOUS;
}
static int selected_source_offset(void) {
  return trailing_offset >= 0 && selected_index >= leading_count ? trailing_offset : offset;
}
static int selected_source_index(void) {
  return trailing_offset >= 0 && selected_index >= leading_count ? selected_index - leading_count : selected_index;
}
static bool chooser, library_failed;
static int library_failed_kind;
static char library_notice[256];
static lv_obj_t *library_open, *library_retry;
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
  collection_ref[0] = 0;
  library_cache_invalidate();
  library_failed = false;
  library_notice[0] = 0;
  if (page) {
    memset(page, 0, sizeof(*page));
    page->next_offset = -1;
  }
}
static bool request_current(const request_t *r) {
  char base[128], selected[64];
  if (r->kind == SHORTCUTS_FETCH || r->kind == SHORTCUTS_PLAY)
    return bridge_client_shortcuts_current(&r->shortcuts_context);
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
      if (r.kind == SHORTCUTS_FETCH) {
        result->ok = bridge_client_shortcuts_fetch(&r.shortcuts_context, r.offset, &result->shortcuts);
      } else if (r.kind == SHORTCUTS_PLAY) {
        result->ok = bridge_client_shortcuts_play(&r.shortcuts_context, r.ref, &result->shortcuts);
      } else if (r.kind == IMAGE) {
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
  for (int i = 0; i < CONTROLLER_LIBRARY_CAPACITY; i++) {
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
  pending_shortcuts = false;
  if (screen == CONTROLLER_SCREEN_SHORTCUTS) {
    shortcut_index = shortcut_offset = 0;
    if (shortcuts) { memset(shortcuts, 0, sizeof(*shortcuts)); shortcuts->next_offset = -1; }
    pending_shortcuts = true;
  }
  if (screen == CONTROLLER_SCREEN_LIBRARY) {
    /* Parent/page changes are deferred while an old network request drains. */
    pending_browse = true;
  }
  render();
}
static void attach_cover(int i) {
  lv_image_dsc_t *d = &covers[i].descriptor;
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_RGB565;
  d->header.w = d->header.h = COVER_SIZE;
  d->header.stride = COVER_SIZE * 2;
  d->data_size = COVER_BYTES;
  d->data = (uint8_t *)covers[i].pixels;
  if (cover_views[i]) lv_image_set_src(cover_views[i], d);
}
static void selected_cover(void) {
  if (screen != CONTROLLER_SCREEN_LIBRARY || chooser || !page || busy || pending_browse || depth == 0) return;
  /* Fetch only the focused/visible region and two neighboring entries. Cache
   * bytes are bounded by the two-page data window, not the catalogue size. */
  for (int step = -1; step < page->count; step++) {
    int i = step < 0 ? selected_index : step;
    if (i < 0 || i >= page->count || (i != selected_index && !rows[i] && abs(i - selected_index) > 2) ||
        covers[i].attempted || !*page->items[i].image) continue;
    for (int j = 0; j < page->count; j++) {
      if (j != i && covers[j].pixels && !strcmp(page->items[j].image, page->items[i].image)) {
        covers[i].pixels = heap_caps_calloc(1, COVER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        covers[i].attempted = true;
        if (covers[i].pixels) { memcpy(covers[i].pixels, covers[j].pixels, COVER_BYTES); attach_cover(i); }
        else covers[i].failed = true;
        break;
      }
    }
    if (covers[i].attempted) continue;
    if (submit(IMAGE, NULL, page->items[i].image, i)) covers[i].attempted = true;
    return;
  }
}
static void library_focus_labels(int index) {
  if (screen != CONTROLLER_SCREEN_LIBRARY || chooser || !page) return;
  static lv_anim_t marquee;
  static bool configured;
  if (!configured) {
    lv_anim_init(&marquee);
    lv_anim_set_delay(&marquee, 1200);
    lv_anim_set_repeat_delay(&marquee, 2000);
    lv_anim_set_repeat_count(&marquee, LV_ANIM_REPEAT_INFINITE);
    configured = true;
  }
  bool current = !pending_browse && !(busy && busy_kind == BROWSE) && !library_failed;
  for (int i = 0; i < page->count; i++) {
    if (!rows[i]) continue;
    for (int child = 0; child < 2; child++) {
      lv_obj_t *text = lv_obj_get_child(rows[i], child);
      if (!text) continue;
      lv_label_long_mode_t mode = current && i == index ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
      if (lv_label_get_long_mode(text) == mode) continue;
      if (mode == LV_LABEL_LONG_SCROLL_CIRCULAR)
        lv_obj_set_style_anim(text, &marquee, LV_PART_MAIN);
      lv_label_set_long_mode(text, mode);
    }
  }
  if (counter && page->count)
    lv_label_set_text_fmt(counter, "%d", selected_source_offset() + selected_source_index() + 1);
}
static void focus_row(int index) {
  if (screen == CONTROLLER_SCREEN_LIBRARY && virtual_library && page && index >= 0 && index < page->count) {
    /* New objects do not have their final content height until layout runs.
     * Reading it earlier scheduled a 72px scroll on an untouched first page. */
    lv_obj_update_layout(scroller);
    library_virtual_refresh();
    lv_obj_update_layout(scroller);
    lv_anim_enable_t motion = rebuilding ? LV_ANIM_OFF : LV_ANIM_ON;
    int y = library_item_y(index), bottom = y + library_row_height(index);
    int scroll = lv_obj_get_scroll_y(scroller), height = lv_obj_get_content_height(scroller);
    if (y < scroll) lv_obj_scroll_to_y(scroller, y, motion);
    else if (bottom > scroll + height) lv_obj_scroll_to_y(scroller, bottom - height, motion);
    for (int i = 0; i < page->count; i++)
      if (rows[i]) lv_obj_set_style_bg_color(rows[i], lv_color_hex(i == index ? 0x39434b : 0x181818), 0);
    library_focus_labels(index);
    return;
  }
  if (!scroller || index < 0 || index >= TOUCH_ROW_COUNT ||
      !rows[index])
    return;
  for (int i = 0; i < TOUCH_ROW_COUNT; i++)
    if (rows[i])
      lv_obj_set_style_bg_color(
          rows[i], lv_color_hex(i == index ? 0x39434b : 0x181818), 0);
  lv_obj_scroll_to_view(rows[index], LV_ANIM_ON);
  library_focus_labels(index);
}
static void move_item(int delta) {
  if (!delta || !page) return;
  if (pending_browse || (busy && busy_kind == BROWSE)) {
    /* Preserve separate actuator events while a page is in flight, as well
     * as the excess from one coalesced event crossing a page boundary. */
    int64_t absolute = desired_absolute >= 0 ? desired_absolute :
                       (int64_t)offset + selected_index;
    absolute += delta;
    desired_absolute = absolute < 0 ? 0 : absolute > INT_MAX ? INT_MAX : (int)absolute;
    return;
  }
  if (chooser || library_failed) {
    if (scroller) {
      int64_t distance = -(int64_t)delta * 36;
      lv_obj_scroll_by(scroller, 0, distance < -360 ? -360 : distance > 360 ? 360 : (int)distance, LV_ANIM_ON);
    }
    return;
  }
  if (library_busy() || !page->count) return;
  library_notice[0] = 0;
  if (subtitle) lv_label_set_text(subtitle, "Turn or swipe");
  int64_t next = (int64_t)(waiting_selection ? wanted_index : selected_index) + delta;
  if (next > 1000000) next = 1000000;
  if (next < -1000000) next = -1000000;
  prefetch_back = delta < 0 && offset > 0;
  if (next < 0 && offset == 0) next = 0;
  if (next >= page->count && page->next_offset < 0) next = page->count - 1;
  waiting_selection = next < 0 || next >= page->count;
  wanted_index = (int)next;
  selected_index = next < 0 ? 0 : next >= page->count ? page->count - 1 : (int)next;
  if (prefetch_failed) prefetch_failed = false; /* a deliberate scroll retries */
  focus_row(selected_index);
}

static void back_action(void) {
  library_failed = false;
  library_notice[0] = 0;
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
    snprintf(collection_ref, sizeof(collection_ref), "%s", history[depth].collection_ref);
    library_cache_invalidate();
    generation++;
    pending_browse = true;
    clear_cover();
    render();
  } else
    change_screen(CONTROLLER_SCREEN_MENU);
}
static void open_item(void) {
  if (pending_browse || library_busy() || !page ||
      selected_index >= page->count)
    return;
  controller_library_item_t *item = &page->items[selected_index];
  if (*item->location) {
    if (depth == HISTORY_MAX) {
      lv_label_set_text(subtitle, "Use Back before opening more folders.");
      return;
    }
    snprintf(history[depth].location, sizeof(history[depth].location), "%s",
             location);
    history[depth].offset = selected_source_offset();
    history[depth].selected_index = selected_source_index();
    snprintf(history[depth].collection_ref, sizeof(history[depth].collection_ref), "%s", collection_ref);
    snprintf(history_names[depth], sizeof(history_names[depth]), "%s",
             folder_name);
    snprintf(folder_name, sizeof(folder_name), "%s", item->title);
    snprintf(collection_ref, sizeof(collection_ref), "%s", item->ref);
    library_cache_invalidate();
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
    library_failed = false;
    library_notice[0] = 0;
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
  ACTION_SHORTCUTS,
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
  ACTION_RETRY_LIBRARY,
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
  if (action == ACTION_REFRESH && screen != CONTROLLER_SCREEN_SHORTCUTS) { refresh_connection(); return; }
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
  if (action == ACTION_SHORTCUTS) {
    menu_index = 0;
    change_screen(CONTROLLER_SCREEN_SHORTCUTS);
    return;
  }
  if (action == ACTION_LIBRARY) {
    menu_index = 1;
    change_screen(CONTROLLER_SCREEN_LIBRARY);
    return;
  }
  if (action == ACTION_ZONES) {
    menu_index = 2;
    change_screen(CONTROLLER_SCREEN_ZONES);
    return;
  }
  if (action == ACTION_SETTINGS) {
    menu_index = 3;
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
  if (screen == CONTROLLER_SCREEN_SHORTCUTS) {
    if (busy || pending_shortcuts || !shortcuts) return;
    if (action == ACTION_REFRESH) { pending_shortcuts = true; return; }
    if (action == ACTION_NEXT && shortcuts->next_offset >= 0) {
      shortcut_offset = shortcuts->next_offset;
      shortcut_index = 0; pending_shortcuts = true;
    } else if (action == ACTION_PREVIOUS && shortcut_offset > 0) {
      shortcut_offset = shortcut_offset > CONTROLLER_SHORTCUTS_PAGE_SIZE ? shortcut_offset - CONTROLLER_SHORTCUTS_PAGE_SIZE : 0;
      shortcut_index = 0; pending_shortcuts = true;
    } else if (action >= ACTION_ROW && action - ACTION_ROW < shortcuts->count) {
      shortcut_index = action - ACTION_ROW;
      submit(SHORTCUTS_PLAY, shortcuts->items[shortcut_index].id, NULL, -1);
    }
    render(); return;
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
  if (pending_browse || library_busy() || !page)
    return;
  if (action == ACTION_RETRY_LIBRARY) {
    library_failed = false;
    library_notice[0] = 0;
    chooser = false;
    library_cache_invalidate();
    generation++;
    pending_browse = true;
    render();
    return;
  }
  if (library_failed) return;
  const char *play_ref = !chooser && *collection_ref ? collection_ref :
                         page->count ? page->items[selected_index].ref : "";
  if (!page->count && !((action == ACTION_PLAY || action == ACTION_QUEUE) && *play_ref)) {
    generation++;
    pending_browse = true;
    render();
    return;
  }
  if (action >= ACTION_ROW) {
    if (virtual_library && (lv_event_get_target(event) != pressed_library_row ||
        action - ACTION_ROW != pressed_library_index || library_data_epoch != pressed_library_epoch ||
        generation != pressed_library_generation)) return;
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
      *play_ref) {
    if (mutation_locked || pending_mutation >= 0)
      return;
    library_failed = false;
    library_notice[0] = 0;
    pending_mutation = action == ACTION_PLAY ? PLAY : QUEUE;
    pending_mutation_generation = generation;
    snprintf(pending_ref, sizeof(pending_ref), "%s",
             play_ref);
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
static void list_scroll(lv_event_t *event) {
  if (rebuilding || (carousel && screen == CONTROLLER_SCREEN_LIBRARY))
    return;
  if (lv_event_get_code(event) == LV_EVENT_SCROLL_BEGIN) {
    lv_indev_t *input = lv_indev_active();
    pointer_scrolling =
        input && lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER;
    if (pointer_scrolling) {
      scroll_start_y = scroller ? lv_obj_get_scroll_y(scroller) : 0;
      library_focus_labels(-1);
    }
    return;
  }
  if (!pointer_scrolling || !scroller)
    return;
  pointer_scrolling = false;
  lv_area_t area;
  lv_obj_get_coords(scroller, &area);
  int center = (area.y1 + area.y2) / 2, nearest = -1, distance = 100000;
  for (int i = 0; i < TOUCH_ROW_COUNT; i++)
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
  if (screen == CONTROLLER_SCREEN_LIBRARY) {
    selected_index = nearest;
    prefetch_back = offset > 0 && lv_obj_get_scroll_y(scroller) < scroll_start_y;
    if (prefetch_failed) prefetch_failed = false;
  }
  else if (screen == CONTROLLER_SCREEN_MENU)
    menu_index = nearest;
  else if (screen == CONTROLLER_SCREEN_SETTINGS)
    setting_index = nearest;
  else if (screen == CONTROLLER_SCREEN_ZONES)
    zone_index = nearest;
  else if (screen == CONTROLLER_SCREEN_SHORTCUTS)
    shortcut_index = nearest;
  for (int i = 0; i < TOUCH_ROW_COUNT; i++)
    if (rows[i])
      lv_obj_set_style_bg_color(
          rows[i], lv_color_hex(i == nearest ? 0x39434b : 0x181818), 0);
  library_focus_labels(nearest);
}

static void brightness_changed(lv_event_t *event) {
  brightness = lv_slider_get_value(lv_event_get_target(event));
  platform_touch_set_brightness(brightness);
  lv_label_set_text_fmt(lv_event_get_user_data(event), "%d%%", brightness);
  controller_device_settings_local_edit(CDS_BRIGHTNESS);
    brightness_dirty = true;
  brightness_changed_ms = esp_timer_get_time() / 1000;
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
  /* DOT only ellipsizes once height is bounded; content height can otherwise
   * wrap a long title over the artist. Keep both labels to one font line. */
  lv_obj_set_height(t, lv_font_get_line_height(platform_touch_font_small()));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_label_set_text(t, name);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 8, 3);
  if (detail && *detail) {
    lv_obj_t *d;
    label(&d, b, 34, 228, platform_touch_font_small());
    lv_obj_set_height(d, lv_font_get_line_height(platform_touch_font_small()));
    lv_obj_set_style_text_color(d, lv_color_hex(0xb8c4cd), 0);
    lv_label_set_text(d, detail);
  }
  if (index >= 0 && index < TOUCH_ROW_COUNT)
    rows[index] = b;
  return b;
}
/* A compact, visible navigation row leaves the widest part of the circle
 * to music. The actuator scrolls metadata in Details, never an invisible
 * "select" operation that these targets cannot complete physically. */
static lv_obj_t *library_text(const char *text, const lv_font_t *font, uint32_t color) {
  lv_obj_t *t = lv_label_create(scroller);
  lv_obj_set_width(t, 252);
  lv_obj_set_style_text_font(t, font, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(color), 0);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_label_set_text(t, text);
  return t;
}
/* Cache data in PSRAM, but bound LVGL to nine reusable rows. Top/bottom
 * spacers retain the complete scroll extent, so adding a response does not
 * rebuild the list or take the pointer's scroll target away. */
static int library_row_height(int index) {
  int line = lv_font_get_line_height(platform_touch_font_small());
  int height = *page->items[index].subtitle ? 2 * line + 8 : line + 16;
  if (depth > 0 && *page->items[index].image && height < COVER_SIZE + 8) height = COVER_SIZE + 8;
  return height;
}
static int library_item_y(int index) {
  int y = 0;
  for (int i = 0; i < index; i++) y += library_row_height(i) + 6;
  return y;
}
static void library_row_pressed(lv_event_t *event) {
  pressed_library_row = lv_event_get_target(event);
  pressed_library_index = (int)(intptr_t)lv_event_get_user_data(event);
  pressed_library_epoch = library_data_epoch;
  pressed_library_generation = generation;
}
static void library_virtual_scroll(lv_event_t *event) {
  (void)event;
  library_virtual_refresh();
}
static lv_obj_t *library_spacer(void) {
  lv_obj_t *o = lv_obj_create(scroller);
  lv_obj_set_width(o, 252);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return o;
}
static void library_virtual_refresh(void) {
  if (!virtual_library || updating_visible || !page || !scroller) return;
  int scroll = lv_obj_get_scroll_y(scroller), first = 0;
  while (first + 1 < page->count && library_item_y(first + 1) <= scroll) first++;
  first = first > 2 ? first - 2 : 0;
  if (first + LIBRARY_VISIBLE_ROWS > page->count)
    first = page->count > LIBRARY_VISIBLE_ROWS ? page->count - LIBRARY_VISIBLE_ROWS : 0;
  if (first == visible_first && page->count == visible_count && visible_rows[0].epoch == library_data_epoch) return;
  updating_visible = true;
  memset(rows, 0, sizeof(rows));
  memset(cover_views, 0, sizeof(cover_views));
  int count = page->count - first;
  if (count > LIBRARY_VISIBLE_ROWS) count = LIBRARY_VISIBLE_ROWS;
  int top = library_item_y(first);
  lv_obj_set_height(top_spacer, top ? top - 6 : 0);
  if (top) lv_obj_remove_flag(top_spacer, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(top_spacer, LV_OBJ_FLAG_HIDDEN);
  for (int slot = 0; slot < LIBRARY_VISIBLE_ROWS; slot++) {
    lv_obj_t *row = visible_rows[slot].row;
    if (slot >= count) { lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
    int index = first + slot;
    const controller_library_item_t *item = &page->items[index];
    if (visible_rows[slot].index != index || visible_rows[slot].epoch != library_data_epoch) {
      int old = visible_rows[slot].index;
      lv_obj_remove_event_cb_with_user_data(row, tap, (void *)(intptr_t)(ACTION_ROW + old));
      lv_obj_remove_event_cb(row, library_row_pressed);
      lv_obj_add_event_cb(row, tap, LV_EVENT_CLICKED, (void *)(intptr_t)(ACTION_ROW + index));
      lv_obj_add_event_cb(row, library_row_pressed, LV_EVENT_PRESSED, (void *)(intptr_t)index);
      lv_obj_t *title = lv_obj_get_child(row, 0), *artist = lv_obj_get_child(row, 1);
      lv_obj_t *image = lv_obj_get_child(row, 2);
      bool thumbnail = depth > 0 && *item->image;
      lv_obj_set_width(title, thumbnail ? 176 : 228);
      lv_obj_set_width(artist, thumbnail ? 176 : 228);
      lv_image_set_src(image, NULL);
      if (thumbnail) {
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(image, LV_ALIGN_LEFT_MID, 4, 0);
        if (covers[index].pixels) lv_image_set_src(image, &covers[index].descriptor);
      } else lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
      char title_line[sizeof(item->title)], detail_line[sizeof(item->subtitle)];
      snprintf(title_line, sizeof(title_line), "%s", item->title);
      snprintf(detail_line, sizeof(detail_line), "%s", item->subtitle);
      for (char *c = title_line; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
      for (char *c = detail_line; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
      lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
      lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
      lv_label_set_text(title, title_line); lv_label_set_text(artist, detail_line);
      int line = lv_font_get_line_height(platform_touch_font_small());
      lv_obj_set_height(title, line); lv_obj_set_height(artist, line);
      lv_obj_align(title, LV_ALIGN_TOP_LEFT, thumbnail ? 60 : 8, *detail_line ? 0 : (library_row_height(index) - 8 - line) / 2);
      lv_obj_align(artist, LV_ALIGN_TOP_LEFT, thumbnail ? 60 : 8, line);
      if (*detail_line) lv_obj_remove_flag(artist, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(artist, LV_OBJ_FLAG_HIDDEN);
      visible_rows[slot].index = index; visible_rows[slot].epoch = library_data_epoch;
    }
    lv_obj_set_height(row, library_row_height(index));
    if (pending_browse || (busy && busy_kind == BROWSE) || library_failed) lv_obj_add_state(row, LV_STATE_DISABLED);
    else lv_obj_remove_state(row, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(row, lv_color_hex(index == selected_index ? 0x39434b : 0x181818), 0);
    rows[index] = row;
    if (depth > 0 && *item->image) cover_views[index] = lv_obj_get_child(row, 2);
  }
  int end = first + count;
  int bottom = library_item_y(page->count) - library_item_y(end);
  lv_obj_set_height(bottom_spacer, bottom ? bottom - 6 : 0);
  if (bottom) lv_obj_remove_flag(bottom_spacer, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(bottom_spacer, LV_OBJ_FLAG_HIDDEN);
  visible_first = first; visible_count = page->count;
  updating_visible = false;
  if (!pointer_scrolling) library_focus_labels(selected_index);
}
static bool library_pointer_down(void) {
  lv_indev_t *input = NULL;
  while ((input = lv_indev_get_next(input)))
    if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) return true;
  return false;
}
static bool library_cache_apply(bool backwards) {
  controller_library_page_t *cached = backwards ? cached_previous : cached_next;
  if (!cached || !(backwards ? previous_cached : next_cached) || chooser || library_failed || pending_browse || !page->count) return false;
  if (backwards ? !prefetch_back || selected_index > LIBRARY_PREFETCH_AHEAD : selected_index < page->count - LIBRARY_PREFETCH_AHEAD && !waiting_selection) return false;
  int old_scroll = scroller ? lv_obj_get_scroll_y(scroller) : 0, shift = 0;
  bool resume_scroll = scroller && lv_obj_is_scrolling(scroller);
  if (backwards) {
    if (pointer_scrolling || library_pointer_down()) return false;
    if (leading_count) {
      int drop = page->count - leading_count;
      if (!cached_next) return false;
      cached_next->count = drop; cached_next->next_offset = page->next_offset;
      memcpy(cached_next->items, page->items + leading_count, drop * sizeof(page->items[0]));
      next_cached_offset = trailing_offset; next_cached = true;
      page->count = leading_count;
    }
    int add = cached->count;
    if (add + page->count > CONTROLLER_LIBRARY_CAPACITY) return false;
    clear_cover();
    trailing_offset = offset;
    memmove(page->items + add, page->items, page->count * sizeof(page->items[0]));
    memcpy(page->items, cached->items, add * sizeof(page->items[0]));
    page->next_offset = next_cached ? next_cached_offset : page->next_offset;
    page->count += add; leading_count = add; offset = previous_cached_offset;
    selected_index += add; if (waiting_selection) wanted_index += add;
    previous_cached = false; prefetch_back = false;
    shift = library_item_y(add);
    library_data_epoch++;
  } else {
    int add = cached->count;
    /* Keep exactly two response chunks, even if a provider returns fewer
     * than the requested 50. Otherwise source offsets stop matching rows. */
    if (leading_count) {
      if (!leading_count || selected_index < leading_count || pointer_scrolling || library_pointer_down() || !cached_previous) return false;
      int drop = leading_count;
      cached_previous->count = drop; cached_previous->next_offset = trailing_offset;
      memcpy(cached_previous->items, page->items, drop * sizeof(page->items[0]));
      previous_cached_offset = offset; previous_cached = true;
      shift = -library_item_y(drop);
      clear_cover();
      memmove(page->items, page->items + drop, (page->count - drop) * sizeof(page->items[0]));
      page->count -= drop; offset = trailing_offset;
      selected_index -= drop; if (waiting_selection) wanted_index -= drop;
      leading_count = page->count;
      library_data_epoch++;
    } else if (!leading_count) leading_count = page->count;
    trailing_offset = next_cached_offset;
    memcpy(page->items + page->count, cached->items, add * sizeof(page->items[0]));
    page->count += add; page->next_offset = cached->next_offset;
    next_cached = false;
  }
  visible_count = -1;
  library_virtual_refresh();
  lv_obj_update_layout(scroller);
  if (shift) {
    lv_obj_scroll_to_y(scroller, old_scroll + shift < 0 ? 0 : old_scroll + shift, LV_ANIM_OFF);
    library_virtual_refresh();
  }
  if (waiting_selection) {
    selected_index = wanted_index < 0 ? 0 : wanted_index >= page->count ? page->count - 1 : wanted_index;
    if (wanted_index >= 0 && wanted_index < page->count) waiting_selection = false;
    if (wanted_index >= page->count && page->next_offset < 0) waiting_selection = false;
    focus_row(selected_index);
  } else if (shift && resume_scroll) focus_row(selected_index);
  else library_focus_labels(selected_index);
  if (subtitle) lv_label_set_text(subtitle, "");
  return true;
}
static void library_prefetch(void) {
  if (chooser || library_failed || pending_browse || !page->count || prefetch_failed) return;
  if (!cached_next) cached_next = heap_caps_calloc(1, sizeof(*cached_next), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!cached_previous) cached_previous = heap_caps_calloc(1, sizeof(*cached_previous), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!cached_next || !cached_previous) {
    prefetch_failed = true;
    if (subtitle) lv_label_set_text(subtitle, "Memory low");
    return;
  }
  if (library_cache_apply(prefetch_back)) return;
  if (prefetch_back && offset > 0 && selected_index <= LIBRARY_PREFETCH_AHEAD && !previous_cached) {
    submit(PREFETCH_PREVIOUS, NULL, NULL, offset > CONTROLLER_LIBRARY_PAGE_SIZE ? offset - CONTROLLER_LIBRARY_PAGE_SIZE : 0);
  } else if (page->next_offset >= 0 && selected_index >= page->count - LIBRARY_PREFETCH_AHEAD && !next_cached) {
    submit(PREFETCH_NEXT, NULL, NULL, page->next_offset);
  }
}
static void render_library(void) {
  bool loading = pending_browse || (busy && busy_kind == BROWSE);
  bool sending = pending_mutation >= 0 || (busy && (busy_kind == PLAY || busy_kind == QUEUE));
  primary = secondary = library_open = library_retry = NULL;
  carousel = false;
  back = button(overlay, -86, 40, 72, ACTION_BACK);
  lv_obj_set_height(back, 44);
  button_text(back, "Back");
  lv_obj_t *home = button(overlay, 86, 40, 72, ACTION_HOME);
  lv_obj_set_height(home, 44);
  button_text(home, "Home");
  label(&heading, overlay, 48, 92, platform_touch_font_small());
  lv_obj_set_height(heading, lv_font_get_line_height(platform_touch_font_small()));
  lv_label_set_text(heading, library_failed ? "Notice" : chooser ? "Details" : loading ? "Loading" : folder_name);
  label(&subtitle, overlay, 316, 160, platform_touch_font_small());
  lv_obj_set_height(subtitle, lv_font_get_line_height(platform_touch_font_small()));
  label(&counter, overlay, 12, 120, platform_touch_font_small());
  lv_obj_set_height(counter, lv_font_get_line_height(platform_touch_font_small()));
  lv_label_set_text(counter, "");
  scroller = lv_obj_create(overlay);
  lv_obj_set_size(scroller, 276, chooser || library_failed || *collection_ref ? 176 : 216);
  lv_obj_align(scroller, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_bg_opa(scroller, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroller, 0, 0);
  lv_obj_set_style_pad_all(scroller, 4, 0);
  lv_obj_set_style_pad_row(scroller, 6, 0);
  lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scroller, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_AUTO);
  if (library_failed) library_text(*library_notice ? library_notice : "Could not load library. Try refreshing.", platform_touch_font_small(), 0xeeeeee);
  if (chooser && page && page->count && selected_index < page->count) {
    const controller_library_item_t *item = &page->items[selected_index];
    library_text(item->title, platform_touch_font_normal(), 0xeeeeee);
    if (*item->subtitle) library_text(item->subtitle, platform_touch_font_small(), 0xb8c4cd);
    char room[90] = "Selected room";
    int count = bridge_client_get_zones(zone_rows, BRIDGE_CLIENT_MAX_ZONES);
    for (int i = 0; i < count; i++)
      if (!strcmp(zone_rows[i].id, zone)) {
        snprintf(room, sizeof(room), "Room: %s", zone_rows[i].name);
        break;
      }
    library_text(room, platform_touch_font_small(), 0xb8c4cd);
    if (library_failed) {
      library_retry = button(scroller, 0, 0, 188, ACTION_RETRY_LIBRARY);
      button_text(library_retry, "Retry library");
      if (library_failed_kind != BROWSE) {
        primary = button(overlay, 0, 274, 160, ACTION_CONTROLS);
        button_text(primary, "Now playing");
        lv_label_set_text(subtitle, "Check playback");
      }
    } else {
      if (*item->ref) {
        primary = button(overlay, -52, 274, 96, ACTION_PLAY); button_text(primary, "Play");
        secondary = button(overlay, 52, 274, 96, ACTION_QUEUE); button_text(secondary, "Queue");
      }
      lv_label_set_text(subtitle, sending ? "Sending…" : "Turn or swipe");
      if (sending || mutation_locked) {
        if (primary) lv_obj_add_state(primary, LV_STATE_DISABLED);
        if (secondary) lv_obj_add_state(secondary, LV_STATE_DISABLED);
      }
      if (sending && library_open) lv_obj_add_state(library_open, LV_STATE_DISABLED);
    }
  } else {
    if (page && page->count) {
      virtual_library = true;
      visible_first = visible_count = -1;
      memset(visible_rows, 0, sizeof(visible_rows));
      top_spacer = library_spacer();
      for (int slot = 0; slot < LIBRARY_VISIBLE_ROWS; slot++) {
        lv_obj_t *row = button(scroller, 0, 0, 252, ACTION_ROW - 1);
        lv_obj_t *artist;
        label(&artist, row, 28, 228, platform_touch_font_small());
        lv_obj_set_style_text_color(artist, lv_color_hex(0xb8c4cd), 0);
        lv_obj_t *image = lv_image_create(row);
        lv_obj_set_size(image, COVER_SIZE, COVER_SIZE);
        lv_obj_set_style_bg_opa(image, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(image, lv_color_hex(0x181818), 0);
        lv_obj_set_style_radius(image, 6, 0);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
        visible_rows[slot].row = row;
        visible_rows[slot].index = -1;
      }
      bottom_spacer = library_spacer();
      lv_obj_add_event_cb(scroller, library_virtual_scroll, LV_EVENT_SCROLL, NULL);
      /* Initial composition runs inside render's rebuild guard. */
      bool guard = rebuilding; rebuilding = false;
      library_virtual_refresh();
      rebuilding = guard;
      if (library_failed) lv_obj_scroll_to_y(scroller, 0, LV_ANIM_OFF);
      else focus_row(selected_index);
      lv_label_set_text_fmt(counter, "%d", selected_source_offset() + selected_source_index() + 1);
    } else {
      library_text(loading ? "Opening your library…" : page && *page->notice ? page->notice : "No items in this collection.", platform_touch_font_small(), 0xeeeeee);
      if (!loading) {
        library_retry = button(scroller, 0, 0, 188, ACTION_RETRY_LIBRARY);
        button_text(library_retry, "Refresh");
      }
    }
    if (library_failed && page && page->count) {
      if (library_failed_kind == BROWSE) {
        library_retry = button(overlay, 0, 274, 160, ACTION_RETRY_LIBRARY);
        button_text(library_retry, "Retry library");
      } else {
        /* A command can finish after Back closes Details. Its uncertainty
         * still needs a direct verification route from the retained list. */
        primary = button(overlay, 0, 274, 160, ACTION_CONTROLS);
        button_text(primary, "Now playing");
        library_retry = button(scroller, 0, 0, 188, ACTION_RETRY_LIBRARY);
        button_text(library_retry, "Retry library");
      }
    }
    if (!library_failed && *collection_ref) {
      primary = button(overlay, -52, 274, 96, ACTION_PLAY); button_text(primary, "Play");
      secondary = button(overlay, 52, 274, 96, ACTION_QUEUE); button_text(secondary, "Queue");
      if (sending || mutation_locked || loading) {
        lv_obj_add_state(primary, LV_STATE_DISABLED); lv_obj_add_state(secondary, LV_STATE_DISABLED);
      }
    }
    lv_label_set_text(subtitle, sending ? "Sending…" : library_failed && library_failed_kind != BROWSE ? "Check playback" : library_failed ? "Refresh library" : loading ? "Loading…" : *library_notice ? library_notice : "");
  }
  if (primary) lv_obj_set_height(primary, 44);
  if (secondary) lv_obj_set_height(secondary, 44);
  if (library_open) lv_obj_set_height(library_open, 44);
  lv_obj_add_event_cb(scroller, list_scroll, LV_EVENT_SCROLL_BEGIN, NULL);
  lv_obj_add_event_cb(scroller, list_scroll, LV_EVENT_SCROLL_END, NULL);
}
static void render(void) {
  ensure_overlay();
  rebuilding = true;
  virtual_library = false;
  pressed_library_row = NULL;
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
  if (screen == CONTROLLER_SCREEN_LIBRARY) {
    render_library();
    rebuilding = false;
    return;
  }
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
    list_row("Shortcuts", NULL, ACTION_SHORTCUTS, 0);
    list_row("Library", NULL, ACTION_LIBRARY, 1);
    char current[64] = "", name[100] = "Pick a zone";
    bridge_client_get_current_zone_id(current, sizeof(current));
    int n = bridge_client_get_zones(zone_rows, BRIDGE_CLIENT_MAX_ZONES);
    for (int i = 0; i < n; i++)
      if (!strcmp(zone_rows[i].id, current))
        snprintf(name, sizeof(name), "Zone: %s", zone_rows[i].name);
    list_row(name, NULL, ACTION_ZONES, 2);
    list_row("Settings", NULL, ACTION_SETTINGS, 3);

    focus_row(menu_index);
  } else if (screen == CONTROLLER_SCREEN_SHORTCUTS) {
    lv_label_set_text(heading, "Shortcuts");
    char zone_name[100] = "Choose a zone first";
    int n = bridge_client_get_zones(zone_rows, BRIDGE_CLIENT_MAX_ZONES);
    for (int i = 0; i < n; i++)
      if (!strcmp(zone_rows[i].id, zone)) snprintf(zone_name, sizeof(zone_name), "Play in %s", zone_rows[i].name);
    lv_label_set_text(subtitle, busy ? (busy_kind == SHORTCUTS_PLAY ? "Sending…" : "Loading…") : pending_shortcuts ? "Loading…" : zone_name);
    if (shortcuts && !busy && !pending_shortcuts) {
      for (int i = 0; i < shortcuts->count; i++)
        list_row(shortcuts->items[i].name, shortcuts->items[i].provider, ACTION_ROW + i, i);
      if (!shortcuts->count) {
        lv_label_set_text(subtitle, zone_name);
        lv_obj_t *message = lv_label_create(scroller);
        lv_obj_set_width(message, 236);
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(message, platform_touch_font_small(), 0);
        lv_obj_set_style_text_color(message, lv_color_hex(0xb8c4cd), 0);
        lv_label_set_text(message, shortcuts->notice);
        if (!shortcuts->accepted) list_row("Retry", NULL, ACTION_REFRESH, 0);
      }
      if (shortcut_offset > 0) list_row("Previous", NULL, ACTION_PREVIOUS, shortcuts->count);
      if (shortcuts->next_offset >= 0) list_row("More shortcuts", NULL, ACTION_NEXT, shortcuts->count + (shortcut_offset > 0));
      focus_row(shortcut_index);
    }
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
  }
  lv_obj_add_event_cb(scroller, list_scroll, LV_EVENT_SCROLL_BEGIN, NULL);
  lv_obj_add_event_cb(scroller, list_scroll, LV_EVENT_SCROLL_END, NULL);
  rebuilding = false;
}
static bool submit(int kind, const char *ref, const char *image,
                   int image_index) {
  if (busy || !requests)
    return false;
  if ((kind == PLAY || kind == QUEUE || kind == SHORTCUTS_PLAY) && mutation_locked)
    return false;
  request_t *r =
      heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!r)
    return false;
  r->generation = kind == IMAGE || kind == PREFETCH_NEXT || kind == PREFETCH_PREVIOUS ? generation : ++generation;
  r->item_index = image_index;
  r->kind = kind;
  r->offset = kind == PREFETCH_NEXT || kind == PREFETCH_PREVIOUS ? image_index : offset;
  if (kind == SHORTCUTS_FETCH || kind == SHORTCUTS_PLAY) {
    r->offset = shortcut_offset;
    bool valid = kind == SHORTCUTS_FETCH ? bridge_client_shortcuts_capture(&r->shortcuts_context) :
        (r->shortcuts_context = shortcuts_context, bridge_client_shortcuts_current(&r->shortcuts_context));
    if (!valid) {
      if (shortcuts) { shortcuts->count = 0; snprintf(shortcuts->notice, sizeof(shortcuts->notice), "Connect and choose a zone first."); }
      free(r); render(); return false;
    }
  }
  if (!bridge_client_get_request_base(r->endpoint, sizeof(r->endpoint)) ||
      !bridge_client_get_current_zone_id(r->zone, sizeof(r->zone))) {
    free(r);
    if (page)
      snprintf(page->notice, sizeof(page->notice),
               "Pick a zone and connect first.");
    if (screen == CONTROLLER_SCREEN_LIBRARY && page) {
      library_failed = true;
      library_failed_kind = BROWSE;
      snprintf(library_notice, sizeof(library_notice), "%s", page->notice);
    }
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
  if (busy && (kind == PLAY || kind == QUEUE || kind == SHORTCUTS_PLAY)) {
    mutation_locked = true;
    mutation_sent_ms = esp_timer_get_time() / 1000;
  }
  free(r);
  if (kind != IMAGE && kind != PREFETCH_NEXT && kind != PREFETCH_PREVIOUS) {
    clear_cover();
    render();
  }
  return busy;
}
void controller_touch_navigation_process(void) {
  if (!shortcuts) shortcuts = heap_caps_calloc(1, sizeof(*shortcuts), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!shortcuts) return;
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
    if (screen == CONTROLLER_SCREEN_SHORTCUTS) {
      shortcuts->count = 0; pending_shortcuts = true; render();
    }
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
      if (result->request.kind == SHORTCUTS_FETCH) {
        *shortcuts = result->shortcuts;
        shortcuts_context = result->request.shortcuts_context;
        render();
      } else if (result->request.kind == SHORTCUTS_PLAY) {
        if (result->ok && !result->shortcuts.show_notice) change_screen(CONTROLLER_SCREEN_ARTWORK);
        else {
          *shortcuts = result->shortcuts;
          shortcuts->count = 0;
          shortcuts->next_offset = -1;
          snprintf(shortcuts->notice, sizeof(shortcuts->notice), "%s", result->shortcuts.notice);
          render();
        }
      } else if (result->request.kind == IMAGE) {
        int image_index = result->request.item_index;
        if (!result->ok && image_index >= 0 && image_index < page->count) {
          covers[image_index].failed = true;

        }
        if (result->ok && screen == CONTROLLER_SCREEN_LIBRARY) {
          int i = result->request.item_index;
          if (i >= 0 && i < page->count && !strcmp(result->request.image, page->items[i].image)) {
            covers[i].pixels = result->pixels;
            result->pixels = NULL;
            attach_cover(i);
          }
        }
      } else if (result->request.kind == PREFETCH_NEXT || result->request.kind == PREFETCH_PREVIOUS) {
        bool backwards = result->request.kind == PREFETCH_PREVIOUS;
        if (!strcmp(result->request.location, location)) {
          filter_library(&result->page);
          if (result->ok && result->page.count) {
            if (backwards && cached_previous && result->request.offset < offset) {
              *cached_previous = result->page; previous_cached_offset = result->request.offset; previous_cached = true;
            } else if (!backwards && cached_next && result->request.offset == page->next_offset) {
              *cached_next = result->page; next_cached_offset = result->request.offset; next_cached = true;
            }
          } else if (result->ok && !backwards) {
            page->next_offset = result->page.next_offset > result->request.offset ? result->page.next_offset : -1;
            if (page->next_offset < 0) waiting_selection = false;
          } else {
            prefetch_failed = true;
            if (subtitle) lv_label_set_text(subtitle, "Turn to retry");
          }
        }
      } else if (result->request.kind == BROWSE) {
        if (result->ok) {
          library_cache_invalidate();
          library_data_epoch++;
          *page = result->page;
          library_failed = false;
          library_notice[0] = 0;
        } else {
          library_failed = true;
          library_failed_kind = BROWSE;
          snprintf(library_notice, sizeof(library_notice), "%s", result->page.notice);
          if (!page->count) snprintf(page->notice, sizeof(page->notice), "%s", library_notice);
        }
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
          library_failed = false;
          if (result->request.kind == QUEUE) {
            chooser = false;
            snprintf(library_notice, sizeof(library_notice), "Queued");
            render();
          } else {
            /* Keep the location/selection even when Play leaves browsing. */
            change_screen(CONTROLLER_SCREEN_NOW_PLAYING);
          }
        } else {
          library_failed = true;
          library_failed_kind = result->request.kind;
          snprintf(library_notice, sizeof(library_notice), "%s", result->page.notice);
          render();
        }
      }
    } else if (screen == CONTROLLER_SCREEN_LIBRARY && !pending_browse &&
               !result) {
      library_failed = true;
      library_failed_kind = busy_kind;
      snprintf(library_notice, sizeof(library_notice), "Not enough memory. Refresh library.");
      if (!page->count) snprintf(page->notice, sizeof(page->notice), "%s", library_notice);
      render();
    }
    if (result && screen == CONTROLLER_SCREEN_SHORTCUTS &&
        (result->request.kind == SHORTCUTS_FETCH || result->request.kind == SHORTCUTS_PLAY) &&
        !bridge_client_shortcuts_current(&result->request.shortcuts_context)) {
      shortcuts->count = 0; pending_shortcuts = true;
    }
    if (!result && screen == CONTROLLER_SCREEN_SHORTCUTS) {
      shortcuts->count = 0;
      snprintf(shortcuts->notice, sizeof(shortcuts->notice), "Not enough memory. Tap Retry."); render();
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
        c.last_success_ms > (uint64_t)mutation_sent_ms) {
      mutation_locked = false;
      if (screen == CONTROLLER_SCREEN_LIBRARY && chooser && !library_failed) render();
    }
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
    controller_device_settings_local_edit(CDS_BRIGHTNESS);
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
  if (screen == CONTROLLER_SCREEN_SHORTCUTS && !busy) {
    if (!pending_shortcuts && shortcuts->count && !bridge_client_shortcuts_current(&shortcuts_context)) {
      shortcuts->count = 0; pending_shortcuts = true;
    }
    if (pending_shortcuts) {
      pending_shortcuts = false;
      submit(SHORTCUTS_FETCH, NULL, NULL, -1);
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
    } else {
      library_prefetch();
      if (!busy) selected_cover();
    }
  }
}
bool controller_touch_navigation_encoder(int delta) {
  if (platform_touch_device_controls_visible()) { platform_touch_scroll_device_controls(delta); return true; }
  if (screen == CONTROLLER_SCREEN_ARTWORK && recovery_active) return true;
  if (screen == CONTROLLER_SCREEN_MENU) {
    menu_index += delta;
    if (menu_index < 0)
      menu_index = 0;
    if (menu_index > 3)
      menu_index = 3;
    focus_row(menu_index);
    return true;
  }
  if (screen == CONTROLLER_SCREEN_SHORTCUTS) {
    int count = shortcuts ? shortcuts->count + (shortcut_offset > 0) + (shortcuts->next_offset >= 0) : 0;
    if (!count) count = 1;
    shortcut_index += delta;
    if (shortcut_index < 0) shortcut_index = 0;
    if (shortcut_index >= count) shortcut_index = count - 1;
    focus_row(shortcut_index); return true;
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
