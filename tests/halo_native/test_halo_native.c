/* Exercise production HALO widgets with real LVGL pointer/scroll processing.
 * Only hardware/network/RTOS boundaries are substituted. No music is played. */
#include "../../common/controller_touch_navigation.c"
#include "controller_touch_contact.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static lv_obj_t *s_zone_header;
static int legacy_zone_taps;
static void zone_label_event_cb(lv_event_t *e) { (void)e; legacy_zone_taps++; }
static void zone_label_long_press_cb(lv_event_t *e) { (void)e; legacy_zone_taps++; }
#include "header_touch_removal.h"
struct test_queue {
  size_t size;
  bool occupied;
  unsigned char *value;
};
QueueHandle_t xQueueCreate(unsigned capacity, size_t size) {
  (void)capacity;
  QueueHandle_t q = calloc(1, sizeof(*q));
  q->size = size;
  q->value = calloc(1, size);
  return q;
}
int xQueueSend(QueueHandle_t q, const void *value, unsigned timeout) {
  (void)timeout;
  if (q->occupied)
    return 0;
  memcpy(q->value, value, q->size);
  q->occupied = true;
  return 1;
}
int xQueueReceive(QueueHandle_t q, void *out, unsigned timeout) {
  (void)timeout;
  if (!q->occupied)
    return 0;
  memcpy(out, q->value, q->size);
  q->occupied = false;
  return 1;
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, int priority, void *handle) {
  (void)task;
  (void)name;
  (void)stack;
  (void)arg;
  (void)priority;
  (void)handle;
  return 1;
}
static uint32_t now;
static bool fixture_ap, fixture_has_ip = true, fixture_ready = true;
static bool fixture_connection_override, fixture_selection_persists = true;
static controller_connection_t fixture_connection;
static controller_config_durability_t fixture_durability = CONTROLLER_CONFIG_DURABILITY_DURABLE;
int64_t esp_timer_get_time(void) { return (int64_t)now * 1000; }
bool bridge_client_get_request_base(char *out, size_t n) {
  snprintf(out, n, "http://bridge:8088");
  return true;
}
static char fixture_zone[64] = "roon:room";
bool bridge_client_get_current_zone_id(char *out, size_t n) {
  snprintf(out, n, "%s", fixture_zone);
  return true;
}
static unsigned fixture_shortcuts_epoch = 1;
bool bridge_client_shortcuts_capture(bridge_shortcuts_context_t *out) {
  memset(out, 0, sizeof(*out));
  out->endpoint.endpoint_generation = fixture_shortcuts_epoch;
  bridge_client_get_request_base(out->endpoint.base, sizeof(out->endpoint.base));
  bridge_client_get_current_zone_id(out->zone, sizeof(out->zone));
  return fixture_ready;
}
bool bridge_client_shortcuts_current(const bridge_shortcuts_context_t *context) {
  return context->endpoint.endpoint_generation == fixture_shortcuts_epoch && !strcmp(context->zone, fixture_zone);
}
bool bridge_client_shortcuts_fetch(const bridge_shortcuts_context_t *context, int start, controller_shortcuts_page_t *out) {
  (void)context; (void)start; (void)out; return false;
}
bool bridge_client_shortcuts_play(const bridge_shortcuts_context_t *context, const char *id, controller_shortcuts_page_t *out) {
  (void)context; (void)id; (void)out; return false;
}
bool bridge_client_get_bridge_url(char *out, size_t n) {
  return bridge_client_get_request_base(out, n);
}
int bridge_client_get_zones(bridge_zone_t *out, int max) {
  int count = max < 64 ? max : 64;
  for (int i = 0; i < count; i++) {
    snprintf(out[i].id, sizeof(out[i].id), "roon:room%d", i);
    snprintf(out[i].name, sizeof(out[i].name), "Listening room %d", i + 1);
  }
  return count;
}
bridge_zone_selection_result_t bridge_client_select_zone_value(const char *id) {
  if (fixture_selection_persists) {
    snprintf(fixture_zone, sizeof(fixture_zone), "%s", id);
    fixture_connection.selected_zone_available = true;
  }
  return (bridge_zone_selection_result_t){.found=true, .persisted=fixture_selection_persists};
}
void bridge_client_connection_snapshot(controller_connection_t *out) {
  memset(out, 0, sizeof(*out));
  out->last_success_ms = now;
  out->reachable = out->zones_current = out->selected_zone_available = fixture_ready;
  out->zone_count = fixture_ready ? 64 : -1;
  if (fixture_connection_override) *out = fixture_connection;
}
void bridge_client_request_poll(void) {}
void bridge_client_connection_status(char *summary, size_t n, char *detail,
                                     size_t m) {
  snprintf(summary, n, "Connected");
  snprintf(detail, m, "Connected to music bridge");
}
bool controller_config_snapshot(controller_config_snapshot_t *out) {
  memset(out, 0, sizeof(*out));
  out->durability = fixture_durability;
  snprintf(out->value.ssid, sizeof(out->value.ssid), "Listening room Wi-Fi");
  out->value.wifi_count = 1;
  snprintf(out->value.wifi[0].ssid, sizeof(out->value.wifi[0].ssid), "Listening room Wi-Fi");
  return true;
}
bool wifi_mgr_get_ip(char *out, size_t n) {
  snprintf(out, n, "%s", fixture_has_ip ? "192.168.1.50" : "");
  return fixture_has_ip;
}
extern const lv_font_t lato_22, notosans_28;
const lv_font_t *platform_touch_font_small(void) { return &lato_22; }
const lv_font_t *platform_touch_font_normal(void) { return &notosans_28; }
static bool device_controls_visible;
static int device_scroll_delta;
const char *platform_product_name(void) {
#ifdef TEST_DIAL_TOUCH
  return "HiPhi Dial";
#else
  return "HiPhi HALO";
#endif
}
void platform_touch_screen_changed(controller_screen_t screen) { (void)screen;device_controls_visible=false; }
bool platform_touch_has_device_controls(void) {
#ifdef TEST_DIAL_TOUCH
  return true;
#else
  return false;
#endif
}
void platform_touch_show_device_controls(void) { device_controls_visible=true; }
bool platform_touch_device_controls_visible(void) { return device_controls_visible; }
void platform_touch_scroll_device_controls(int delta) { device_scroll_delta+=delta; }
static uint32_t local_setting_edits;
void controller_device_settings_local_edit(uint32_t fields) { local_setting_edits |= fields; }
static int saved_brightness = 100, brightness_saves;
void platform_touch_set_brightness(int percent) { (void)percent; }
int platform_touch_load_brightness(void) { return saved_brightness; }
bool platform_touch_save_brightness(int percent) { saved_brightness = percent; brightness_saves++; return true; }

bool wifi_mgr_is_ap_mode(void) { return fixture_ap; }
bool wifi_mgr_start_provisioning(void) { fixture_ap = true; return true; }
const char *wifi_mgr_get_hostname(void) { return "hiphi-halo-test"; }
const char *wifi_mgr_get_last_error(void) { return NULL; }
int wifi_mgr_get_retry_count(void) { return 2; }
int wifi_mgr_get_retry_max(void) { return 5; }
const char *platform_provisioning_ssid(void) { return "hiphi-halo-setup"; }
static int wifi_saves, bridge_saves;
controller_config_write_result_t controller_config_upsert_wifi(const char *ssid, const char *pass, bool promote, controller_config_snapshot_t *out) { (void)pass;(void)promote;(void)out;assert(*ssid);wifi_saves++;return CONTROLLER_CONFIG_COMMITTED_VERIFIED; }
bool controller_config_wifi_snapshot(controller_config_wifi_snapshot_t *out) { memset(out,0,sizeof(*out));return true; }
void wifi_mgr_apply_wifi(const controller_config_wifi_snapshot_t *networks, bool reconnect) { (void)networks;(void)reconnect; }
static int forgotten_networks;
controller_config_write_result_t controller_config_remove_wifi_named(const char *ssid, controller_config_snapshot_t *out) { assert(*ssid);(void)out;forgotten_networks++;return CONTROLLER_CONFIG_COMMITTED_VERIFIED; }
controller_config_write_result_t controller_config_promote_wifi_named(const char *ssid, controller_config_snapshot_t *out) { assert(*ssid);(void)out;return CONTROLLER_CONFIG_COMMITTED_VERIFIED; }
controller_config_write_result_t controller_config_set_endpoint(const char *base, bool automatic, controller_config_snapshot_t *out) { (void)automatic;(void)out;assert(!*base || !strncmp(base,"http://",7));bridge_saves++;return CONTROLLER_CONFIG_COMMITTED_VERIFIED; }
void ui_hide_zone_picker(void) {}
bool ui_is_zone_picker_visible(void) { return false; }
void ui_zone_picker_scroll(int delta) { (void)delta; }
static bool artwork_only, controls_visible;
static void (*background_tap_handler)(void);
static int reset_calls;
void ui_set_background_tap_handler(void (*handler)(void)) { background_tap_handler = handler; }
void wifi_mgr_forget_wifi(void) { reset_calls++; }
const char *portal_brand_firmware_version(void) { return "test-version"; }
void ui_set_controls_visible(bool visible) { controls_visible = visible; }
void ui_set_artwork_only(bool only) { artwork_only = only; }
int platform_http_get_bounded(const char *url, size_t maximum, char **out,
                              size_t *len) {
  (void)url;
  (void)maximum;
  (void)out;
  (void)len;
  return -1;
}
int platform_http_post_json(const char *url, const char *body, char **out,
                            size_t *len) {
  (void)url;
  (void)body;
  (void)out;
  (void)len;
  return -1;
}
void platform_http_free(char *pixels) { free(pixels); }
static uint8_t frame[360 * 360 * 3];
static uint8_t draw[360 * 24 * 3];
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *pixels) {
  int width = area->x2 - area->x1 + 1;
  for (int y = area->y1; y <= area->y2; y++)
    memcpy(frame + (y * 360 + area->x1) * 3,
           pixels + (y - area->y1) * width * 3, width * 3);
  lv_display_flush_ready(d);
}
static lv_indev_data_t pointer;
static void read_pointer(lv_indev_t *i, lv_indev_data_t *out) {
  (void)i;
  *out = pointer;
}
static void advance(unsigned ms) {
  for (unsigned t = 0; t < ms; t += 10) {
    now += 10;
    lv_tick_inc(10);
    lv_timer_handler();
  }
}
static void input(int x, int y, bool pressed, unsigned ms) {
  pointer.point = (lv_point_t){x, y};
  pointer.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  advance(ms);
}
static void click(lv_obj_t *obj) {
  lv_obj_update_layout(obj);
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
  if (lv_display_get_rotation(lv_display_get_default()) == LV_DISPLAY_ROTATION_180) { x=359-x;y=359-y; }
  input(x, y, true, 80);
  input(x, y, false, 80);
}
static void snapshot(const char *dir, const char *name) {
  if (!dir)
    return;
  advance(400);
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n360 360\n255\n");
  for (int y = 0; y < 360; y++)
    for (int x = 0; x < 360; x++) {
      int dx = x - 180, dy = y - 180;
      uint8_t black[3] = {0};
      uint8_t *pixel = frame + (y * 360 + x) * 3;
      uint8_t rgb[3] = {pixel[2], pixel[1], pixel[0]};
      fwrite(dx * dx + dy * dy > 180 * 180 ? black : rgb, 1, 3, f);
    }
  fclose(f);
}
int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : NULL;
  lv_init();
  static _Alignas(8) uint8_t pool[72 * 1024];
  assert(lv_mem_add_pool(pool, sizeof(pool)));
  lv_display_t *d = lv_display_create(360, 360);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB888);
  lv_display_set_buffers(d, draw, NULL, sizeof(draw),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush);
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_pointer);
  shortcuts = calloc(1, sizeof(*shortcuts));
  page = calloc(1, sizeof(*page));
  page->next_offset = -1;
  requests = xQueueCreate(1, sizeof(request_t));
  results = xQueueCreate(1, sizeof(result_t *));
  bridge_client_get_request_base(endpoint, sizeof(endpoint));
  bridge_client_get_current_zone_id(zone, sizeof(zone));
  s_zone_header = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_zone_header, 300, 95);
  lv_obj_align(s_zone_header, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_add_event_cb(s_zone_header, zone_label_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(s_zone_header, zone_label_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
  change_screen(CONTROLLER_SCREEN_ARTWORK);
  advance(100);
  assert(!lv_obj_has_flag(s_zone_header, LV_OBJ_FLAG_CLICKABLE));
  assert(!lv_obj_has_flag(s_zone_header, LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_send_event(s_zone_header, LV_EVENT_CLICKED, NULL);
  lv_obj_send_event(s_zone_header, LV_EVENT_LONG_PRESSED, NULL);
  assert(legacy_zone_taps == 0);
  lv_obj_delete(s_zone_header);
  s_zone_header = NULL;
  assert(artwork_only);
  assert(lv_obj_has_flag(home_button, LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(art_touch, LV_OBJ_FLAG_HIDDEN));
  click(art_touch);
  assert(screen == CONTROLLER_SCREEN_NOW_PLAYING);
  assert(!artwork_only);
  assert(!lv_obj_has_flag(home_button, LV_OBJ_FLAG_HIDDEN));
  assert(background_tap_handler);
  background_tap_handler();
  assert(screen == CONTROLLER_SCREEN_ARTWORK && artwork_only);
  assert(!background_tap_handler);
  assert(!controller_touch_navigation_encoder(1)); /* Same detent adjusts volume. */
  assert(screen == CONTROLLER_SCREEN_NOW_PLAYING);
  assert(controls_visible && !artwork_only);
  controller_touch_navigation_restore_visibility();
  assert(controls_visible && !artwork_only);
  change_screen(CONTROLLER_SCREEN_MENU);
  advance(100);
  snapshot(dir, "menu");
  assert(!strcmp(lv_label_get_text(lv_obj_get_child(rows[0], 0)), "Shortcuts"));
  strcpy(fixture_zone, "roon:room0");
  strcpy(zone, fixture_zone);
  click(rows[0]);
  assert(screen == CONTROLLER_SCREEN_SHORTCUTS && pending_shortcuts);
  controller_touch_navigation_process();
  request_t shortcut_request;
  assert(xQueueReceive(requests, &shortcut_request, 0));
  assert(shortcut_request.kind == SHORTCUTS_FETCH);
  result_t *shortcut_result = calloc(1, sizeof(*shortcut_result));
  shortcut_result->request = shortcut_request;
  shortcut_result->ok = true;
  shortcut_result->shortcuts.count = 2;
  shortcut_result->shortcuts.next_offset = -1;
  strcpy(shortcut_result->shortcuts.items[0].id, "sc_playlist");
  strcpy(shortcut_result->shortcuts.items[0].name, "Evening listening");
  strcpy(shortcut_result->shortcuts.items[0].provider, "Roon");
  strcpy(shortcut_result->shortcuts.items[1].id, "sc_album");
  strcpy(shortcut_result->shortcuts.items[1].name, "Kind of Blue");
  strcpy(shortcut_result->shortcuts.items[1].provider, "LMS");
  xQueueSend(results, &shortcut_result, 0);
  controller_touch_navigation_process(); advance(300);
  assert(shortcuts->count == 2 && !busy);
  snapshot(dir, "shortcuts");
  controller_touch_navigation_encoder(1);
  assert(shortcut_index == 1);
  click(rows[0]);
  assert(xQueueReceive(requests, &shortcut_request, 0));
  assert(shortcut_request.kind == SHORTCUTS_PLAY && !strcmp(shortcut_request.ref, "sc_playlist"));
  /* A second tap while the first is pending never enqueues another play. */
  if (rows[0]) click(rows[0]);
  assert(!requests->occupied);
  shortcut_result = calloc(1, sizeof(*shortcut_result));
  shortcut_result->request = shortcut_request;
  strcpy(shortcut_result->shortcuts.notice, "Playback uncertain. Check now playing.");
  xQueueSend(results, &shortcut_result, 0);
  controller_touch_navigation_process();
  assert(screen == CONTROLLER_SCREEN_SHORTCUTS && shortcuts->count == 0);
  assert(strstr(shortcuts->notice, "uncertain"));
  snapshot(dir, "shortcut-uncertain");
  controller_touch_navigation_process();
  assert(!requests->occupied); /* no automatic replay */
  /* An accepted duplicate selection shows its notice without offering a replay. */
  shortcut_result = calloc(1, sizeof(*shortcut_result));
  shortcut_result->request = shortcut_request;
  shortcut_result->ok = true;
  shortcut_result->shortcuts.accepted = true;
  shortcut_result->shortcuts.show_notice = true;
  shortcut_result->shortcuts.next_offset = -1;
  strcpy(shortcut_result->shortcuts.notice, "Multiple matches found. Playing the first exact match.");
  xQueueSend(results, &shortcut_result, 0);
  controller_touch_navigation_process();
  assert(screen == CONTROLLER_SCREEN_SHORTCUTS && shortcuts->accepted);
  assert(strstr(shortcuts->notice, "Multiple matches"));
  assert(!rows[0]); /* no Retry that would send another play */
  snapshot(dir, "shortcut-multiple-matches");
  click(back);
  assert(screen == CONTROLLER_SCREEN_MENU);
  busy = mutation_locked = pending_shortcuts = false;
  change_screen(CONTROLLER_SCREEN_SETTINGS);
  advance(300);
  snapshot(dir, "settings");
  lv_obj_scroll_to_view(rows[4], LV_ANIM_OFF);
  advance(100);
  click(rows[4]);
  assert(settings_about && !settings_reset);
  click(back);
  assert(!settings_about && screen == CONTROLLER_SCREEN_SETTINGS);
  lv_obj_scroll_to_view(rows[5], LV_ANIM_OFF);
  advance(100);
  click(rows[5]);
  assert(settings_reset && reset_calls == 0);
  click(back);
  assert(!settings_reset && reset_calls == 0);
  lv_obj_scroll_to_view(rows[5], LV_ANIM_OFF);
  advance(100);
  click(rows[5]);
  click(primary);
  assert(reset_calls == 1);
  change_screen(CONTROLLER_SCREEN_SETTINGS);
  /* Slow finger remains a real press. Drag scrolls Settings, never changes page
   * or activates the brightness row underneath the finger. */
  input(180, 145, true, 650);
  assert(lv_obj_has_state(rows[0], LV_STATE_PRESSED));
  input(180, 120, true, 60);
  input(180, 95, true, 60);
  input(180, 75, true, 60);
  input(180, 75, false, 500);
  assert(screen == CONTROLLER_SCREEN_SETTINGS);
  assert(brightness == 100);
  assert(lv_obj_get_scroll_y(scroller) > 0);
  setting_index = 0;
  controller_touch_navigation_encoder(3);
  advance(300);
  assert(setting_index == 3);
  change_screen(CONTROLLER_SCREEN_ZONES);
  controller_touch_navigation_encoder(63);
  advance(400);
  assert(zone_index == 63);
  assert(lv_obj_get_scroll_y(scroller) > 0);
  snapshot(dir, "zones");
  /* Text library with unique opaque playback handles and supplied images. */
  page->count = 4;
  for (int i = 0; i < 4; i++) {
    snprintf(page->items[i].title, sizeof(page->items[i].title), "Album %d",
             i + 1);
    snprintf(page->items[i].image, sizeof(page->items[i].image),
             "/api/collections/image?ref=art%d", i);
    snprintf(page->items[i].ref, sizeof(page->items[i].ref), "album-ref-%d", i);
  }
  screen = CONTROLLER_SCREEN_LIBRARY;
  pending_browse = false;
  busy = false;
  render();
  advance(300);
  snapshot(dir, "albums");
  controller_touch_navigation_encoder(3);
  advance(500);
  assert(selected_index == 3);
  assert(!carousel); /* Cover-bearing rows remain readable text lists. */
  assert(lv_obj_get_scroll_y(scroller) > 0);
  assert(pending_mutation == -1);
  click(rows[selected_index]);
  assert(chooser);
  /* Play sends the exact selected opaque handle. */
  request_t in_flight;
  xQueueReceive(requests, &in_flight, 0);
  busy = true;
  busy_kind = IMAGE;
  click(primary);
  assert(pending_mutation == PLAY);
  int played = selected_index;
  busy = false;
  controller_touch_navigation_process();
  request_t request;
  assert(xQueueReceive(requests, &request, 0));
  assert(request.kind == PLAY);
  assert(!strcmp(request.ref, page->items[played].ref));
  busy = true;
  busy_kind = IMAGE;
  depth = 0;
  chooser = false;
  screen = CONTROLLER_SCREEN_LIBRARY;
  pending_mutation = -1;
  render();
  click(back);
  assert(screen == CONTROLLER_SCREEN_MENU);
  /* Navigating away cancels a deliberate action that was waiting only for
   * artwork IO. It must not fire later in a different listening context. */
  screen = CONTROLLER_SCREEN_LIBRARY;
  mutation_locked =
      false; /* A separate scenario after fresh listening state. */
  busy = true;
  busy_kind = IMAGE;
  chooser = true;
  render();
  click(primary);
  assert(pending_mutation == PLAY);
  change_screen(CONTROLLER_SCREEN_SETTINGS);
  busy = false;
  controller_touch_navigation_process();
  assert(!xQueueReceive(requests, &request, 0));
  /* Back from a leaf's action chooser also cancels, not only top-level Home. */
  screen = CONTROLLER_SCREEN_LIBRARY;
  chooser = true;
  busy = true;
  busy_kind = IMAGE;
  render();
  click(primary);
  assert(pending_mutation == PLAY);
  click(back);
  assert(!chooser && pending_mutation == -1);
  busy = false;
  controller_touch_navigation_process();
  if (xQueueReceive(requests, &request, 0))
    assert(request.kind == IMAGE);
  /* A saved playlist is both navigable and playable. Tapping it exposes
   * actions; Play/Queue retain the collection ref, Open browses its location. */
  busy = false;
  mutation_locked = false;
  chooser = false;
  selected_index = 0;
  page->count = 1;
  snprintf(page->items[0].title, sizeof(page->items[0].title), "Evening playlist");
  snprintf(page->items[0].location, sizeof(page->items[0].location), "playlist-location");
  snprintf(page->items[0].ref, sizeof(page->items[0].ref), "whole-playlist-ref");
  render();
  click(rows[0]);
  assert(chooser && !pending_browse);
  snapshot(dir, "playlist-actions");
  click(secondary);
  controller_touch_navigation_process();
  assert(xQueueReceive(requests, &request, 0));
  assert(request.kind == QUEUE && !strcmp(request.ref, "whole-playlist-ref"));
  /* Detents during the sending screen must not dereference removed buttons. */
  assert(controller_touch_navigation_encoder(1));
  busy = false;
  mutation_locked = false;
  pending_mutation = -1;
  render();
  click(primary);
  controller_touch_navigation_process();
  assert(xQueueReceive(requests, &request, 0));
  assert(request.kind == PLAY && !strcmp(request.ref, "whole-playlist-ref"));
  busy = false;
  mutation_locked = false;
  pending_mutation = -1;
  render();
  /* The collection title is the Open action, separate from playback. */
  click(lv_obj_get_child(scroller, 0));
  assert(pending_browse && !chooser);
  assert(!strcmp(location, "playlist-location"));
  pending_browse = false;
  page->items[0].location[0] = 0;
  /* A stale image result never attaches pixels to the new page. */
  busy = true;
  busy_kind = IMAGE;
  result_t *stale = calloc(1, sizeof(*stale));
  stale->request.generation = generation - 1;
  stale->request.kind = IMAGE;
  stale->ok = true;
  stale->pixels = calloc(1, COVER_BYTES);
  snprintf(stale->request.endpoint, sizeof(stale->request.endpoint), "%s",
           endpoint);
  snprintf(stale->request.zone, sizeof(stale->request.zone), "%s", zone);
  assert(xQueueSend(results, &stale, 0));
  controller_touch_navigation_process();
  assert(!covers[0].pixels);
  xQueueReceive(requests, &request, 0);
  busy = false;
  /* Excess detents survive a page boundary through the real result handler. */
  page->count = 12;
  page->next_offset = 12;
  selected_index = 11;
  offset = 0;
  pending_browse = false;
  controller_touch_navigation_encoder(3);
  controller_touch_navigation_process();
  assert(xQueueReceive(requests, &request, 0));
  assert(request.kind == BROWSE && request.offset == 12);
  result_t *next = calloc(1, sizeof(*next));
  next->request = request;
  next->ok = true;
  next->page.count = 12;
  next->page.next_offset = -1;
  assert(xQueueSend(results, &next, 0));
  controller_touch_navigation_process();
  assert(offset == 12 && selected_index == 2);
  /* Duplicate artwork URLs retain the requested card index, never overwrite
   * the first card's owned pixels. */
  busy = false;
  pending_browse = false;
  carousel = true;
  selected_index = 2;
  page->count = 4;
  for (int i = 0; i < 4; i++)
    snprintf(page->items[i].image, sizeof(page->items[i].image),
             "/api/collections/image?ref=same");
  clear_cover();
  selected_cover();
  assert(xQueueReceive(requests, &request, 0));
  assert(request.item_index == 2);
  busy = false;
  /* A zone change invalidates an otherwise current old-zone browse result. */
  result_t *old_zone = calloc(1, sizeof(*old_zone));
  old_zone->request.generation = generation;
  old_zone->request.kind = BROWSE;
  snprintf(old_zone->request.endpoint, sizeof(old_zone->request.endpoint), "%s",
           endpoint);
  snprintf(old_zone->request.zone, sizeof(old_zone->request.zone), "%s", zone);
  old_zone->ok = true;
  old_zone->page.count = 1;
  snprintf(old_zone->page.items[0].title, sizeof(old_zone->page.items[0].title),
           "Wrong room");
  snprintf(fixture_zone, sizeof(fixture_zone), "roon:new-room");
  assert(xQueueSend(results, &old_zone, 0));
  controller_touch_navigation_process();
  assert(page->count == 0);
  assert(xQueueReceive(requests, &request, 0));
  assert(!strcmp(request.zone, fixture_zone) && request.kind == BROWSE);
  /* Search is a capability filter, not a substring match against music. */
  controller_library_page_t filtered = {0};
  filtered.count = 4; filtered.next_offset = 12;
  snprintf(filtered.items[0].title, sizeof(filtered.items[0].title), "Search");
  snprintf(filtered.items[0].location, sizeof(filtered.items[0].location), "opaque-search");
  snprintf(filtered.items[0].image, sizeof(filtered.items[0].image), "/api/collections/image?ref=search_icon");
  snprintf(filtered.items[1].title, sizeof(filtered.items[1].title), "Search");
  snprintf(filtered.items[1].ref, sizeof(filtered.items[1].ref), "playable-album");
  snprintf(filtered.items[2].title, sizeof(filtered.items[2].title), "Search Party");
  snprintf(filtered.items[2].location, sizeof(filtered.items[2].location), "collection");
  filtered.items[3].requires_input = true;
  filter_library(&filtered);
  assert(filtered.count == 2 && filtered.next_offset == 12);
  assert(!strcmp(filtered.items[0].ref, "playable-album"));
  assert(!strcmp(filtered.items[1].title, "Search Party"));
  char normalized[128];
  assert(normalize_bridge(" 192.168.1.2 ", normalized, sizeof(normalized)));
  assert(!strcmp(normalized, "http://192.168.1.2:8088"));
  assert(normalize_bridge("http://bridge.local:9000/", normalized, sizeof(normalized)));
  assert(!strcmp(normalized, "http://bridge.local:9000"));
  assert(!normalize_bridge("https://bridge", normalized, sizeof(normalized)));
  assert(!normalize_bridge(":8088", normalized, sizeof(normalized)));
  assert(!normalize_bridge("bridge:0", normalized, sizeof(normalized)));
  assert(!normalize_bridge("bridge/path", normalized, sizeof(normalized)));
  /* Recovery must be visible without discovering the invisible artwork tap. */
  change_screen(CONTROLLER_SCREEN_ARTWORK);
  fixture_ap = true; fixture_has_ip = false;
  advance(1100); controller_touch_navigation_process();
  assert(recovery_active && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
  assert(strstr(lv_label_get_text(detail_label), "192.168.4.1"));
  assert(controller_touch_navigation_encoder(1));
  snapshot(dir, "setup");
  fixture_ap = false; fixture_has_ip = false;
  advance(1100); controller_touch_navigation_process();
  assert(recovery_active && strstr(recovery_text, "Waiting for network"));
  fixture_has_ip = true; fixture_ready = false;
  advance(1100); controller_touch_navigation_process();
  assert(recovery_active && !strcmp(recovery_title, "Music connection"));
  fixture_ready = true;
  advance(1100); controller_touch_navigation_process();
  assert(!recovery_active && lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
  assert(artwork_only);
  /* A reachable bridge with a removed/unselected zone is not an outage. */
  fixture_connection_override = true;
  fixture_connection = (controller_connection_t){.reachable=true, .zones_current=true, .zone_count=7};
  for (int missing = 0; missing < 2; missing++) {
    snprintf(fixture_zone, sizeof(fixture_zone), "%s", missing ? "roon:removed" : "");
    change_screen(CONTROLLER_SCREEN_ARTWORK);
    advance(1100); controller_touch_navigation_process();
    assert(recovery_active && !strcmp(recovery_title, "Choose a zone"));
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(secondary, 0)), "Zones"));
    lv_obj_update_layout(overlay);
    assert(lv_obj_get_self_height(detail_label) <= lv_obj_get_height(detail_label));
    snapshot(dir, "choose-zone");
    click(secondary);
    assert(screen == CONTROLLER_SCREEN_ZONES && zone_count == 64);
    advance(2200); controller_touch_navigation_process();
    assert(screen == CONTROLLER_SCREEN_ZONES); /* polling must not steal navigation */
    fixture_selection_persists = false;
    click(rows[0]);
    assert(screen == CONTROLLER_SCREEN_ZONES);
    fixture_selection_persists = true;
    click(rows[0]);
    advance(1100); controller_touch_navigation_process();
    assert(screen == CONTROLLER_SCREEN_ARTWORK && !recovery_active);
    fixture_connection.selected_zone_available = false;
  }
  /* Stale, ambiguous, offline, missing inventory and empty inventory must not offer Zones. */
  for (int failure = 0; failure < 6; failure++) {
    fixture_connection = (controller_connection_t){.reachable=true, .zones_current=true, .zone_count=7};
    if (failure == 0) fixture_connection.stale = true;
    if (failure == 1) fixture_connection.ambiguous = true;
    if (failure == 2) fixture_connection.offline = true;
    if (failure == 3) fixture_connection.zones_current = false;
    if (failure == 4) fixture_connection.zone_count = 0;
    if (failure == 5) fixture_connection.reachable = false;
    advance(1100); controller_touch_navigation_process();
    assert(recovery_active);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(secondary, 0)), "Zones"));
    lv_obj_update_layout(overlay);
    assert(lv_obj_get_self_height(detail_label) <= lv_obj_get_height(detail_label));
  }
  fixture_connection_override = false;
  advance(1100); controller_touch_navigation_process();
  fixture_durability = CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY;
  advance(1100); controller_touch_navigation_process();
  assert(recovery_active && strstr(recovery_title, "storage"));
  fixture_durability = CONTROLLER_CONFIG_DURABILITY_DURABLE;
  advance(1100); controller_touch_navigation_process();
  assert(!recovery_active);
  change_screen(CONTROLLER_SCREEN_SETTINGS);
  lv_obj_scroll_to_view(rows[1], LV_ANIM_OFF); advance(100); click(rows[1]);
  assert(settings_detail == 1 && strstr(lv_label_get_text(detail_label), "Connected to music bridge"));
  click(primary);
  assert(connection_refresh_pending);
  advance(5100); controller_touch_navigation_process();
  assert(!connection_refresh_pending && !strcmp(settings_notice, "Refresh timed out"));
  snapshot(dir, "connection");
  click(back);
  lv_obj_scroll_to_view(rows[2], LV_ANIM_OFF); advance(100); click(rows[2]);
  assert(settings_detail == 2);
  click(primary);
  assert(edit_kind == 1);
  lv_textarea_set_text(entry, "Test network"); click(primary);
  assert(edit_kind == 2);
  snapshot(dir, "keyboard");
  lv_textarea_set_text(entry, "bad"); click(primary);
  assert(wifi_saves == 0 && edit_kind == 2);
  lv_textarea_set_text(entry, "test-password"); click(primary);
  assert(wifi_saves == 1 && edit_kind == 0 && !*edit_ssid);
  click(back);
  lv_obj_scroll_to_view(rows[3], LV_ANIM_OFF); advance(100); click(rows[3]);
  click(primary);
  assert(edit_kind == 3);
  lv_textarea_set_text(entry, "https://unsupported"); click(primary);
  assert(bridge_saves == 0 && edit_kind == 3);
  lv_textarea_set_text(entry, "bridge.local"); click(primary);
  assert(bridge_saves == 1 && edit_kind == 0);
  click(back);
  lv_obj_scroll_to_view(rows[6], LV_ANIM_OFF); advance(100); click(rows[6]);
  assert(settings_detail == 6);
  lv_obj_scroll_to_view(rows[1], LV_ANIM_OFF); advance(100); click(rows[1]);
  assert(settings_detail == 7 && forgotten_networks == 0);
  click(back);
  assert(settings_detail == 6 && forgotten_networks == 0);
  lv_obj_scroll_to_view(rows[1], LV_ANIM_OFF); advance(100); click(rows[1]);
  click(primary);
  assert(settings_detail == 6 && forgotten_networks == 1);
  click(back);
  local_setting_edits = 0;
  brightness = 42; brightness_dirty = true; brightness_changed_ms = now;
  advance(600); controller_touch_navigation_process();
  assert(brightness_saves == 0);
  advance(200); controller_touch_navigation_process();
  assert(brightness_saves == 1 && platform_touch_load_brightness() == 42);
  assert(local_setting_edits == CDS_BRIGHTNESS);
  /* A page containing only unsupported folders advances to the provider's
   * offset instead of showing an empty/retry page or repeating offset zero. */
  while (xQueueReceive(requests, &request, 0)) {}
  busy = false;
  change_screen(CONTROLLER_SCREEN_LIBRARY);
  offset = 0; selected_index = 0;
  controller_touch_navigation_process();
  assert(xQueueReceive(requests, &request, 0));
  assert(request.kind == BROWSE && request.offset == 0);
  result_t *search_only = calloc(1, sizeof(*search_only));
  search_only->request = request; search_only->ok = true;
  search_only->page.count = 1; search_only->page.next_offset = 12;
  snprintf(search_only->page.items[0].title, sizeof(search_only->page.items[0].title), "Search");
  snprintf(search_only->page.items[0].location, sizeof(search_only->page.items[0].location), "opaque-search");
  assert(xQueueSend(results, &search_only, 0));
  controller_touch_navigation_process();
  assert(page->count == 0 && offset == 12);
  assert(xQueueReceive(requests, &request, 0));
  assert(request.kind == BROWSE && request.offset == 12);
  change_screen(CONTROLLER_SCREEN_ARTWORK);
  controller_touch_contact_t contact = {0};
  assert(!controller_touch_contact_feed(&contact, true, 10, 20, true, false));
  assert(!controller_touch_contact_feed(&contact, true, 30, 40, false, false));
  assert(contact.x == 30 && contact.y == 40);
  assert(!controller_touch_contact_feed(&contact, false, 0, 0, false, false));
  assert(contact.x == 30 && contact.y == 40);
  assert(controller_touch_contact_feed(&contact, true, 50, 60, false, false));
  assert(controller_touch_contact_feed(&contact, true, 50, 5, false, false));
  controller_touch_contact_feed(&contact, false, 0, 0, false, false);
  assert(!controller_touch_contact_feed(&contact, true, 50, 60, false, true));
  assert(!controller_touch_contact_feed(&contact, true, 50, 60, false, false));
  lv_display_set_rotation(d, LV_DISPLAY_ROTATION_180);
  change_screen(CONTROLLER_SCREEN_ARTWORK);
  click(art_touch);
  assert(screen == CONTROLLER_SCREEN_NOW_PLAYING);
  click(home_button);
  assert(screen == CONTROLLER_SCREEN_MENU);
  lv_display_set_rotation(d, LV_DISPLAY_ROTATION_0);
#ifdef TEST_DIAL_TOUCH
  change_screen(CONTROLLER_SCREEN_SETTINGS);
  assert(rows[7]);
  lv_obj_scroll_to_view(rows[7], LV_ANIM_OFF);advance(100);click(rows[7]);
  assert(device_controls_visible);
  assert(controller_touch_navigation_encoder(2));
  assert(device_scroll_delta==2);
  change_screen(CONTROLLER_SCREEN_ARTWORK);
  assert(!device_controls_visible);
#endif
  lv_mem_monitor_t memory;
  lv_mem_monitor(&memory);
  assert(memory.total_size <= 96 * 1024);
  printf("LVGL pool: %zu bytes total, %u%% used\n", memory.total_size,
         memory.used_pct);
  clear_cover();
  lv_deinit();
  free(page);
  free(requests->value);
  free(requests);
  free(results->value);
  free(results);
  puts("HALO native gestures, navigation and playback fencing passed");
  return 0;
}
