/// Manifest-driven knob UI.
///
/// Fresh implementation alongside ui.c, activated via USE_MANIFEST.
/// Style values copied from ui.c for pixel-identical default rendering.
/// Screen manager handles navigation between multiple screens.

#include "manifest_ui.h"
#include "manifest_parse.h"
#include "ui.h"

#include "platform/platform_display.h"
#include "platform/platform_log.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "font_manager.h"
#endif

#define TAG "manifest_ui"

// ── Display constants (from ui.c) ──────────────────────────────────────────

#if defined(ESP_PLATFORM) && CONFIG_LCD_H_RES == 360
#define SCREEN_SIZE 360
#else
#define SCREEN_SIZE 240
#endif

// Colors (exact values from ui.c build_layout)
#define COLOR_BG lv_color_hex(0x000000)
#define COLOR_TEXT_PRIMARY lv_color_hex(0xfafafa)
#define COLOR_TEXT_SECONDARY lv_color_hex(0xaaaaaa)
#define COLOR_TEXT_DIM lv_color_hex(0x888888)
#define COLOR_ZONE_LABEL lv_color_hex(0xbbbbbb)
#define COLOR_ARC_BG lv_color_hex(0x3a3a3a)
#define COLOR_ARC_VOLUME lv_color_hex(0x5a9fd4)
#define COLOR_ARC_PROGRESS_BG lv_color_hex(0x2a2a2a)
#define COLOR_ARC_PROGRESS lv_color_hex(0x7bb9e8)
#define COLOR_STATUS_GREEN lv_color_hex(0x2ecc71)
#define COLOR_STATUS_RED lv_color_hex(0xe74c3c)
#define COLOR_BTN_BG lv_color_hex(0x1a1a1a)
#define COLOR_BTN_PRESSED lv_color_hex(0x3c3c3c)
#define COLOR_BTN_PRIMARY lv_color_hex(0x2c2c2c)
#define COLOR_BTN_BORDER lv_color_hex(0x5a5a5a)
#define COLOR_BTN_BORDER_HL lv_color_hex(0x5a9fd4)
#define COLOR_STATUS_BAR_BG lv_color_hex(0xfafafa)
#define COLOR_STATUS_BAR_TEXT lv_color_hex(0x000000)
#define COLOR_CARD_BG lv_color_hex(0x1a1a1a)

// ── Font wrappers (same as ui.c) ──────────────────────────────────────────

#if !TARGET_PC
static inline const lv_font_t *font_small(void) {
  return font_manager_get_small();
}
static inline const lv_font_t *font_normal(void) {
  return font_manager_get_normal();
}
static inline const lv_font_t *font_large(void) {
  return font_manager_get_large();
}
static inline const lv_font_t *font_icon_normal(void) {
  return font_manager_get_icon_normal();
}
static inline const lv_font_t *font_icon_large(void) {
  return font_manager_get_icon_large();
}
#else
static inline const lv_font_t *font_small(void) {
  return &lv_font_montserrat_20;
}
static inline const lv_font_t *font_normal(void) {
  return &lv_font_montserrat_28;
}
static inline const lv_font_t *font_large(void) {
  return &lv_font_montserrat_48;
}
static inline const lv_font_t *font_icon_normal(void) {
  return &lv_font_montserrat_28;
}
static inline const lv_font_t *font_icon_large(void) {
  return &lv_font_montserrat_48;
}
#endif

// ── State ──────────────────────────────────────────────────────────────────

/// Screen manager state.
static struct {
  int current_screen;         // Index into nav.order
  int screen_count;           // Number of navigable screens
  char sha[MANIFEST_SHA_LEN]; // Last rendered SHA

  // Cached manifest for current screens
  manifest_t manifest;
  bool has_manifest;
} s_mgr;

/// LVGL widget pointers — media screen.
static struct {
  lv_obj_t *container; // Root container for this screen
  lv_obj_t *artwork_image;
  lv_obj_t *volume_arc;
  lv_obj_t *progress_arc;
  lv_obj_t *volume_label;
  lv_obj_t *track_label;  // line[0] = title
  lv_obj_t *artist_label; // line[1] = subtitle
  lv_obj_t *play_icon;
  lv_obj_t *btn_prev;
  lv_obj_t *btn_play;
  lv_obj_t *btn_next;
} s_media;

/// LVGL widget pointers — shared chrome (header, status).
static struct {
  lv_obj_t *screen_root; // Root object on active screen
  lv_obj_t *zone_label;
  lv_obj_t *status_dot;
  lv_obj_t *status_bar;     // Transient message at bottom
  lv_obj_t *network_banner; // Persistent network status
} s_chrome;

/// LVGL widget pointers — list screen.
static struct {
  lv_obj_t *container;
  lv_obj_t *title_label;
  lv_obj_t *list;
  int selected;
} s_list;

/// LVGL widget pointers — card screen.
static struct {
  lv_obj_t *container;
  lv_obj_t *lines[MANIFEST_MAX_LINES];
  int line_count;
} s_card;

// Input callback (same pattern as ui.c)
static ui_input_cb_t s_input_cb = NULL;

// ── Forward declarations ───────────────────────────────────────────────────

static void build_chrome(lv_obj_t *parent);
static void build_media_screen(lv_obj_t *parent);
static void build_list_screen(lv_obj_t *parent);
static void build_card_screen(lv_obj_t *parent);

static void update_media_fast(const manifest_fast_t *fast);
static void update_media_screen(const manifest_media_t *media);
static void update_list_screen(const manifest_list_t *list);
static void update_card_screen(const manifest_card_t *card);

static void show_screen(int index);
static int find_screen_index(const char *screen_id);

static int calculate_volume_percentage(float vol, float vol_min, float vol_max);
static void format_volume_text(char *buf, size_t len, float vol, float vol_min,
                               float vol_step);

// ── Event handlers ─────────────────────────────────────────────────────────

static bool s_zone_long_pressed = false;

static void zone_label_event_cb(lv_event_t *e) {
  (void)e;
  if (s_zone_long_pressed) {
    s_zone_long_pressed = false;
    return;
  }
  if (s_input_cb)
    s_input_cb(UI_INPUT_MENU);
}

static void zone_label_long_press_cb(lv_event_t *e) {
  (void)e;
  s_zone_long_pressed = true;
  // Settings handled by bridge_client via UI_INPUT_MENU long press
}

static void btn_prev_event_cb(lv_event_t *e) {
  (void)e;
  if (s_input_cb)
    s_input_cb(UI_INPUT_PREV_TRACK);
}

static void btn_play_event_cb(lv_event_t *e) {
  (void)e;
  if (s_input_cb)
    s_input_cb(UI_INPUT_PLAY_PAUSE);
}

static void btn_next_event_cb(lv_event_t *e) {
  (void)e;
  if (s_input_cb)
    s_input_cb(UI_INPUT_NEXT_TRACK);
}

// ── Volume helpers (from ui.c) ─────────────────────────────────────────────

static int calculate_volume_percentage(float vol, float vol_min,
                                       float vol_max) {
  if (vol_max <= vol_min)
    return 0;
  float pct = (vol - vol_min) / (vol_max - vol_min) * 100.0f;
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;
  return (int)pct;
}

static void format_volume_text(char *buf, size_t len, float vol, float vol_min,
                               float vol_step) {
  if (vol_min < 0) {
    // dB mode
    if (vol_step < 1.0f) {
      snprintf(buf, len, "%.1f dB", (double)vol);
    } else {
      snprintf(buf, len, "%d dB", (int)vol);
    }
  } else {
    // Percentage mode
    snprintf(buf, len, "%d", (int)vol);
  }
}

// ── Init ───────────────────────────────────────────────────────────────────

void manifest_ui_set_input_handler(ui_input_cb_t handler) {
  s_input_cb = handler;
}

void manifest_ui_init(void) {
  memset(&s_mgr, 0, sizeof(s_mgr));
  memset(&s_media, 0, sizeof(s_media));
  memset(&s_chrome, 0, sizeof(s_chrome));
  memset(&s_list, 0, sizeof(s_list));
  memset(&s_card, 0, sizeof(s_card));

  lv_obj_t *screen = lv_screen_active();
  if (!screen)
    return;

  lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  s_chrome.screen_root = lv_obj_create(screen);
  lv_obj_set_size(s_chrome.screen_root, SCREEN_SIZE, SCREEN_SIZE);
  lv_obj_center(s_chrome.screen_root);
  lv_obj_set_style_bg_opa(s_chrome.screen_root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_chrome.screen_root, 0, 0);
  lv_obj_set_style_pad_all(s_chrome.screen_root, 0, 0);

  // Build all screen containers (hidden by default)
  build_media_screen(s_chrome.screen_root);
  build_list_screen(s_chrome.screen_root);
  build_card_screen(s_chrome.screen_root);

  // Chrome on top of screens
  build_chrome(s_chrome.screen_root);

  // Start on media screen
  s_mgr.current_screen = 0;
  show_screen(0);
}

// ── Chrome (header + status — shared across screens) ───────────────────────

static void build_chrome(lv_obj_t *parent) {
  // Header (zone label at top)
  lv_obj_t *header = lv_obj_create(parent);
  lv_obj_set_size(header, SCREEN_SIZE - 60, 95);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_layout(header, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(header, 0, 0);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 28);

  // Tappable header
  lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(header, zone_label_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(header, zone_label_long_press_cb, LV_EVENT_LONG_PRESSED,
                      NULL);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(header, LV_OPA_50, LV_STATE_PRESSED);

  s_chrome.zone_label = lv_label_create(header);
  lv_label_set_text(s_chrome.zone_label, "");
  lv_obj_set_style_text_font(s_chrome.zone_label, font_small(), 0);
  lv_obj_set_style_text_color(s_chrome.zone_label, COLOR_ZONE_LABEL, 0);
  lv_obj_set_width(s_chrome.zone_label, SCREEN_SIZE - 120);
  lv_obj_set_style_text_align(s_chrome.zone_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_chrome.zone_label, LV_LABEL_LONG_DOT);

  // Status dot
  s_chrome.status_dot = lv_obj_create(parent);
  lv_obj_set_size(s_chrome.status_dot, 10, 10);
  lv_obj_set_style_radius(s_chrome.status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_chrome.status_dot, 0, 0);
  lv_obj_align(s_chrome.status_dot, LV_ALIGN_TOP_RIGHT, -35, 35);
  lv_obj_set_style_bg_color(s_chrome.status_dot, COLOR_STATUS_RED, 0);
  lv_obj_set_style_bg_opa(s_chrome.status_dot, LV_OPA_COVER, 0);

  // Status bar at bottom
  s_chrome.status_bar = lv_label_create(parent);
  lv_label_set_text(s_chrome.status_bar, "");
  lv_obj_set_width(s_chrome.status_bar, SCREEN_SIZE - 60);
  lv_obj_set_style_text_font(s_chrome.status_bar, font_small(), 0);
  lv_obj_set_style_text_align(s_chrome.status_bar, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_chrome.status_bar, COLOR_STATUS_BAR_TEXT, 0);
  lv_label_set_long_mode(s_chrome.status_bar, LV_LABEL_LONG_DOT);
  lv_obj_set_style_bg_color(s_chrome.status_bar, COLOR_STATUS_BAR_BG, 0);
  lv_obj_set_style_bg_opa(s_chrome.status_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_ver(s_chrome.status_bar, 4, 0);
  lv_obj_set_style_pad_hor(s_chrome.status_bar, 12, 0);
  lv_obj_set_style_radius(s_chrome.status_bar, 8, 0);
  lv_obj_align(s_chrome.status_bar, LV_ALIGN_BOTTOM_MID, 0, -25);
}

// ── Media screen builder ───────────────────────────────────────────────────
// Pixel-identical to ui.c build_layout() now_playing section.

static void build_media_screen(lv_obj_t *parent) {
  s_media.container = lv_obj_create(parent);
  lv_obj_set_size(s_media.container, SCREEN_SIZE, SCREEN_SIZE);
  lv_obj_center(s_media.container);
  lv_obj_set_style_bg_opa(s_media.container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_media.container, 0, 0);
  lv_obj_set_style_pad_all(s_media.container, 0, 0);

  // Artwork image (hidden until loaded)
  s_media.artwork_image = lv_img_create(s_media.container);
  lv_obj_set_size(s_media.artwork_image, SCREEN_SIZE, SCREEN_SIZE);
  lv_obj_center(s_media.artwork_image);
  lv_obj_add_flag(s_media.artwork_image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_img_opa(s_media.artwork_image, LV_OPA_40, 0);

  // Volume arc (outer)
  s_media.volume_arc = lv_arc_create(s_media.container);
  lv_obj_set_size(s_media.volume_arc, SCREEN_SIZE - 10, SCREEN_SIZE - 10);
  lv_obj_center(s_media.volume_arc);
  lv_arc_set_range(s_media.volume_arc, 0, 100);
  lv_arc_set_value(s_media.volume_arc, 0);
  lv_arc_set_bg_angles(s_media.volume_arc, 0, 359);
  lv_arc_set_rotation(s_media.volume_arc, 270);
  lv_arc_set_mode(s_media.volume_arc, LV_ARC_MODE_NORMAL);
  lv_obj_set_style_arc_width(s_media.volume_arc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_media.volume_arc, 8, LV_PART_INDICATOR);
  lv_obj_remove_flag(s_media.volume_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(s_media.volume_arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s_media.volume_arc, 0, LV_PART_KNOB);
  lv_obj_set_style_arc_color(s_media.volume_arc, COLOR_ARC_BG, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_media.volume_arc, COLOR_ARC_VOLUME,
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(s_media.volume_arc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_media.volume_arc, LV_OPA_COVER, LV_PART_INDICATOR);

  // Progress arc (inner)
  s_media.progress_arc = lv_arc_create(s_media.container);
  lv_obj_set_size(s_media.progress_arc, SCREEN_SIZE - 30, SCREEN_SIZE - 30);
  lv_obj_center(s_media.progress_arc);
  lv_arc_set_range(s_media.progress_arc, 0, 100);
  lv_arc_set_value(s_media.progress_arc, 0);
  lv_arc_set_bg_angles(s_media.progress_arc, 0, 359);
  lv_arc_set_rotation(s_media.progress_arc, 270);
  lv_arc_set_mode(s_media.progress_arc, LV_ARC_MODE_NORMAL);
  lv_obj_set_style_arc_width(s_media.progress_arc, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_media.progress_arc, 4, LV_PART_INDICATOR);
  lv_obj_remove_flag(s_media.progress_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(s_media.progress_arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s_media.progress_arc, 0, LV_PART_KNOB);
  lv_obj_set_style_arc_color(s_media.progress_arc, COLOR_ARC_PROGRESS_BG,
                             LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_media.progress_arc, COLOR_ARC_PROGRESS,
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(s_media.progress_arc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_media.progress_arc, LV_OPA_COVER,
                           LV_PART_INDICATOR);

  // Now playing content group
  lv_obj_t *np = lv_obj_create(s_media.container);
  lv_obj_set_size(np, SCREEN_SIZE - 80, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(np, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(np, 0, 0);
  lv_obj_set_style_pad_all(np, 0, 0);
  lv_obj_set_layout(np, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(np, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(np, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(np, 6, 0);
  lv_obj_align(np, LV_ALIGN_CENTER, 0, 20);

  // Volume label
  s_media.volume_label = lv_label_create(np);
  lv_label_set_text(s_media.volume_label, "-- dB");
  lv_obj_set_style_text_font(s_media.volume_label, font_normal(), 0);
  lv_obj_set_style_text_color(s_media.volume_label, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_margin_bottom(s_media.volume_label, 4, 0);

  // Artist (line[1] = subtitle)
  s_media.artist_label = lv_label_create(np);
  lv_obj_set_width(s_media.artist_label, SCREEN_SIZE - 100);
  lv_obj_set_style_text_font(s_media.artist_label, font_small(), 0);
  lv_obj_set_style_text_align(s_media.artist_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_media.artist_label, COLOR_TEXT_SECONDARY, 0);
  lv_label_set_long_mode(s_media.artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_anim_time(s_media.artist_label, 25000, LV_PART_MAIN);
  lv_label_set_text(s_media.artist_label, "");

  // Track (line[0] = title)
  s_media.track_label = lv_label_create(np);
  lv_obj_set_width(s_media.track_label, SCREEN_SIZE - 100);
  lv_obj_set_style_text_font(s_media.track_label, font_normal(), 0);
  lv_obj_set_style_text_align(s_media.track_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_media.track_label, COLOR_TEXT_PRIMARY, 0);
  lv_label_set_long_mode(s_media.track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_anim_time(s_media.track_label, 25000, LV_PART_MAIN);
  lv_label_set_text(s_media.track_label, "");

  // Transport controls row
  lv_obj_t *controls = lv_obj_create(np);
  lv_obj_set_size(controls, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(controls, 0, 0);
  lv_obj_set_style_pad_all(controls, 0, 0);
  lv_obj_set_layout(controls, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(controls, 14, 0);
  lv_obj_set_style_margin_top(controls, 8, 0);

  // Previous button
  s_media.btn_prev = lv_btn_create(controls);
  lv_obj_set_size(s_media.btn_prev, 60, 60);
  lv_obj_add_event_cb(s_media.btn_prev, btn_prev_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_set_style_bg_color(s_media.btn_prev, COLOR_BTN_BG, LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(s_media.btn_prev, COLOR_BTN_PRESSED,
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(s_media.btn_prev, 2, 0);
  lv_obj_set_style_border_color(s_media.btn_prev, COLOR_BTN_BORDER,
                                LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(s_media.btn_prev, COLOR_BTN_BORDER_HL,
                                LV_STATE_PRESSED);
  lv_obj_set_style_radius(s_media.btn_prev, LV_RADIUS_CIRCLE, 0);
  lv_obj_t *prev_lbl = lv_label_create(s_media.btn_prev);
#if !TARGET_PC
  lv_label_set_text(prev_lbl, ICON_SKIP_PREV);
  lv_obj_set_style_text_font(prev_lbl, font_icon_normal(), 0);
#else
  lv_label_set_text(prev_lbl, LV_SYMBOL_PREV);
  lv_obj_set_style_text_font(prev_lbl, &lv_font_montserrat_28, 0);
#endif
  lv_obj_set_style_text_color(prev_lbl, COLOR_TEXT_PRIMARY, 0);
  lv_obj_center(prev_lbl);

  // Play/Pause button
  s_media.btn_play = lv_btn_create(controls);
  lv_obj_set_size(s_media.btn_play, 80, 80);
  lv_obj_add_event_cb(s_media.btn_play, btn_play_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_set_style_bg_color(s_media.btn_play, COLOR_BTN_PRIMARY,
                            LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(s_media.btn_play, COLOR_BTN_PRESSED,
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(s_media.btn_play, 2, 0);
  lv_obj_set_style_border_color(s_media.btn_play, COLOR_BTN_BORDER_HL,
                                LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(s_media.btn_play, COLOR_ARC_PROGRESS,
                                LV_STATE_PRESSED);
  lv_obj_set_style_radius(s_media.btn_play, LV_RADIUS_CIRCLE, 0);
  s_media.play_icon = lv_label_create(s_media.btn_play);
#if !TARGET_PC
  lv_label_set_text(s_media.play_icon, ICON_PLAY);
  lv_obj_set_style_text_font(s_media.play_icon, font_icon_large(), 0);
#else
  lv_label_set_text(s_media.play_icon, LV_SYMBOL_PLAY);
  lv_obj_set_style_text_font(s_media.play_icon, &lv_font_montserrat_48, 0);
#endif
  lv_obj_set_style_text_color(s_media.play_icon, COLOR_TEXT_PRIMARY, 0);
  lv_obj_center(s_media.play_icon);

  // Next button
  s_media.btn_next = lv_btn_create(controls);
  lv_obj_set_size(s_media.btn_next, 60, 60);
  lv_obj_add_event_cb(s_media.btn_next, btn_next_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_set_style_bg_color(s_media.btn_next, COLOR_BTN_BG, LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(s_media.btn_next, COLOR_BTN_PRESSED,
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(s_media.btn_next, 2, 0);
  lv_obj_set_style_border_color(s_media.btn_next, COLOR_BTN_BORDER,
                                LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(s_media.btn_next, COLOR_BTN_BORDER_HL,
                                LV_STATE_PRESSED);
  lv_obj_set_style_radius(s_media.btn_next, LV_RADIUS_CIRCLE, 0);
  lv_obj_t *next_lbl = lv_label_create(s_media.btn_next);
#if !TARGET_PC
  lv_label_set_text(next_lbl, ICON_SKIP_NEXT);
  lv_obj_set_style_text_font(next_lbl, font_icon_normal(), 0);
#else
  lv_label_set_text(next_lbl, LV_SYMBOL_NEXT);
  lv_obj_set_style_text_font(next_lbl, &lv_font_montserrat_28, 0);
#endif
  lv_obj_set_style_text_color(next_lbl, COLOR_TEXT_PRIMARY, 0);
  lv_obj_center(next_lbl);
}

// ── List screen builder ────────────────────────────────────────────────────

static void build_list_screen(lv_obj_t *parent) {
  s_list.container = lv_obj_create(parent);
  lv_obj_set_size(s_list.container, SCREEN_SIZE, SCREEN_SIZE);
  lv_obj_center(s_list.container);
  lv_obj_set_style_bg_opa(s_list.container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_list.container, 0, 0);
  lv_obj_set_style_pad_all(s_list.container, 0, 0);
  lv_obj_add_flag(s_list.container, LV_OBJ_FLAG_HIDDEN);

  // Title
  s_list.title_label = lv_label_create(s_list.container);
  lv_obj_set_style_text_font(s_list.title_label, font_normal(), 0);
  lv_obj_set_style_text_color(s_list.title_label, COLOR_TEXT_PRIMARY, 0);
  lv_obj_set_style_text_align(s_list.title_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s_list.title_label, SCREEN_SIZE - 80);
  lv_obj_align(s_list.title_label, LV_ALIGN_TOP_MID, 0, 30);
  lv_label_set_text(s_list.title_label, "");

  // Scrollable list
  s_list.list = lv_list_create(s_list.container);
  lv_obj_set_size(s_list.list, SCREEN_SIZE - 40, SCREEN_SIZE - 90);
  lv_obj_align(s_list.list, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(s_list.list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_list.list, 0, 0);
  lv_obj_set_style_pad_all(s_list.list, 0, 0);
}

// ── Card screen builder ────────────────────────────────────────────────────

static void build_card_screen(lv_obj_t *parent) {
  s_card.container = lv_obj_create(parent);
  lv_obj_set_size(s_card.container, SCREEN_SIZE, SCREEN_SIZE);
  lv_obj_center(s_card.container);
  lv_obj_set_style_bg_opa(s_card.container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_card.container, 0, 0);
  lv_obj_set_style_pad_all(s_card.container, 0, 0);
  lv_obj_add_flag(s_card.container, LV_OBJ_FLAG_HIDDEN);

  // Card content area
  lv_obj_t *card_bg = lv_obj_create(s_card.container);
  lv_obj_set_size(card_bg, SCREEN_SIZE - 60, LV_SIZE_CONTENT);
  lv_obj_center(card_bg);
  lv_obj_set_style_bg_color(card_bg, COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(card_bg, LV_OPA_80, 0);
  lv_obj_set_style_radius(card_bg, 16, 0);
  lv_obj_set_style_border_width(card_bg, 0, 0);
  lv_obj_set_style_pad_all(card_bg, 16, 0);
  lv_obj_set_layout(card_bg, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card_bg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card_bg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card_bg, 8, 0);

  // Pre-create line labels
  for (int i = 0; i < MANIFEST_MAX_LINES; i++) {
    s_card.lines[i] = lv_label_create(card_bg);
    lv_obj_set_width(s_card.lines[i], SCREEN_SIZE - 100);
    lv_obj_set_style_text_align(s_card.lines[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_card.lines[i], LV_LABEL_LONG_DOT);
    lv_label_set_text(s_card.lines[i], "");
    lv_obj_add_flag(s_card.lines[i], LV_OBJ_FLAG_HIDDEN);
  }
}

// ── Screen manager ─────────────────────────────────────────────────────────

static int find_screen_index(const char *screen_id) {
  for (int i = 0; i < s_mgr.manifest.screen_count; i++) {
    if (strcmp(s_mgr.manifest.screens[i].id, screen_id) == 0)
      return i;
  }
  return -1;
}

static lv_obj_t *get_screen_container(screen_type_t type) {
  switch (type) {
  case SCREEN_TYPE_MEDIA:
    return s_media.container;
  case SCREEN_TYPE_LIST:
    return s_list.container;
  case SCREEN_TYPE_CARD:
    return s_card.container;
  default:
    return NULL;
  }
}

static void show_screen(int nav_index) {
  // Hide all screen containers
  if (s_media.container)
    lv_obj_add_flag(s_media.container, LV_OBJ_FLAG_HIDDEN);
  if (s_list.container)
    lv_obj_add_flag(s_list.container, LV_OBJ_FLAG_HIDDEN);
  if (s_card.container)
    lv_obj_add_flag(s_card.container, LV_OBJ_FLAG_HIDDEN);

  if (!s_mgr.has_manifest || nav_index < 0 ||
      nav_index >= s_mgr.manifest.nav.count) {
    // Fallback: show media
    if (s_media.container)
      lv_obj_remove_flag(s_media.container, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const char *screen_id = s_mgr.manifest.nav.order[nav_index];
  int screen_idx = find_screen_index(screen_id);
  if (screen_idx < 0) {
    LOGI("show_screen: screen '%s' not found in manifest", screen_id);
    if (s_media.container)
      lv_obj_remove_flag(s_media.container, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  screen_type_t type = s_mgr.manifest.screens[screen_idx].type;
  lv_obj_t *container = get_screen_container(type);
  if (container) {
    lv_obj_remove_flag(container, LV_OBJ_FLAG_HIDDEN);
  }

  s_mgr.current_screen = nav_index;
}

// ── Update functions ───────────────────────────────────────────────────────

static void update_media_fast(const manifest_fast_t *fast) {
  // Volume arc
  int vol_pct = calculate_volume_percentage(fast->volume, fast->volume_min,
                                            fast->volume_max);
  lv_arc_set_value(s_media.volume_arc, vol_pct);

  // Volume label
  char vol_text[16];
  format_volume_text(vol_text, sizeof(vol_text), fast->volume, fast->volume_min,
                     fast->volume_step);
  lv_label_set_text(s_media.volume_label, vol_text);

  // Progress arc
  if (fast->length > 0) {
    int progress_pct = (fast->seek_position * 100) / fast->length;
    if (progress_pct > 100)
      progress_pct = 100;
    if (progress_pct < 0)
      progress_pct = 0;
    lv_arc_set_value(s_media.progress_arc, progress_pct);
  } else {
    lv_arc_set_value(s_media.progress_arc, 0);
  }

  // Play/pause icon
#if !TARGET_PC
  lv_label_set_text(s_media.play_icon,
                    fast->is_playing ? ICON_PAUSE : ICON_PLAY);
#else
  lv_label_set_text(s_media.play_icon,
                    fast->is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
#endif
}

static void update_media_screen(const manifest_media_t *media) {
  // Track (title — line[0])
  if (media->line_count > 0) {
    lv_label_set_text(s_media.track_label, media->lines[0].text);
  }
  // Artist (subtitle — line[1])
  if (media->line_count > 1) {
    lv_label_set_text(s_media.artist_label, media->lines[1].text);
  }
}

static void update_list_screen(const manifest_list_t *list) {
  lv_label_set_text(s_list.title_label, list->title);

  // Clear existing items
  lv_obj_clean(s_list.list);

  for (int i = 0; i < list->item_count; i++) {
    const manifest_list_item_t *item = &list->items[i];
    lv_obj_t *btn = lv_list_add_btn(s_list.list, NULL, item->label);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(btn, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(btn, font_small(), 0);

    if (item->selected) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    }

    // Store index as user data for event handling
    lv_obj_set_user_data(btn, (void *)(intptr_t)i);
  }
}

static void update_card_screen(const manifest_card_t *card) {
  for (int i = 0; i < MANIFEST_MAX_LINES; i++) {
    if (i < card->line_count) {
      lv_obj_remove_flag(s_card.lines[i], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(s_card.lines[i], card->lines[i].text);

      // Style based on text_style_t
      switch (card->lines[i].style) {
      case TEXT_STYLE_TITLE:
        lv_obj_set_style_text_font(s_card.lines[i], font_normal(), 0);
        lv_obj_set_style_text_color(s_card.lines[i], COLOR_TEXT_PRIMARY, 0);
        break;
      case TEXT_STYLE_SUBTITLE:
        lv_obj_set_style_text_font(s_card.lines[i], font_small(), 0);
        lv_obj_set_style_text_color(s_card.lines[i], COLOR_TEXT_SECONDARY, 0);
        break;
      case TEXT_STYLE_DETAIL:
      default:
        lv_obj_set_style_text_font(s_card.lines[i], font_small(), 0);
        lv_obj_set_style_text_color(s_card.lines[i], COLOR_TEXT_DIM, 0);
        break;
      }
    } else {
      lv_obj_add_flag(s_card.lines[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// ── Public API ─────────────────────────────────────────────────────────────

void manifest_ui_update(const manifest_t *manifest) {
  if (!manifest)
    return;

  // Always apply fast state (volume, seek, transport)
  update_media_fast(&manifest->fast);

  // Check if screens changed (SHA comparison)
  bool screens_changed = (strcmp(s_mgr.sha, manifest->sha) != 0);

  if (screens_changed) {
    LOGI("Manifest SHA changed: '%s' -> '%s', re-rendering %d screens",
         s_mgr.sha, manifest->sha, manifest->screen_count);

    // Cache the new manifest
    s_mgr.manifest = *manifest;
    s_mgr.has_manifest = true;
    strncpy(s_mgr.sha, manifest->sha, sizeof(s_mgr.sha) - 1);

    // Update each screen's content
    for (int i = 0; i < manifest->screen_count; i++) {
      const manifest_screen_t *scr = &manifest->screens[i];
      switch (scr->type) {
      case SCREEN_TYPE_MEDIA:
        update_media_screen(&scr->data.media);
        break;
      case SCREEN_TYPE_LIST:
        update_list_screen(&scr->data.list);
        break;
      case SCREEN_TYPE_CARD:
        update_card_screen(&scr->data.card);
        break;
      default:
        break;
      }
    }

    // Update screen count for navigation
    s_mgr.screen_count = manifest->nav.count;

    // If current screen is invalid, go to default
    if (s_mgr.current_screen >= s_mgr.screen_count) {
      int def = find_screen_index(manifest->nav.default_screen);
      s_mgr.current_screen = (def >= 0) ? def : 0;
    }
    show_screen(s_mgr.current_screen);
  }
}

void manifest_ui_navigate(int delta) {
  if (s_mgr.screen_count <= 1)
    return;

  int next = s_mgr.current_screen + delta;
  // Wrap around
  if (next < 0)
    next = s_mgr.screen_count - 1;
  if (next >= s_mgr.screen_count)
    next = 0;

  show_screen(next);
}

screen_type_t manifest_ui_current_screen_type(void) {
  if (!s_mgr.has_manifest || s_mgr.current_screen >= s_mgr.manifest.nav.count) {
    return SCREEN_TYPE_MEDIA;
  }
  const char *id = s_mgr.manifest.nav.order[s_mgr.current_screen];
  int idx = find_screen_index(id);
  if (idx < 0)
    return SCREEN_TYPE_MEDIA;
  return s_mgr.manifest.screens[idx].type;
}

const char *manifest_ui_current_screen_id(void) {
  if (!s_mgr.has_manifest || s_mgr.current_screen >= s_mgr.manifest.nav.count) {
    return "now_playing";
  }
  return s_mgr.manifest.nav.order[s_mgr.current_screen];
}

void manifest_ui_set_zone_name(const char *name) {
  if (s_chrome.zone_label && name) {
    lv_label_set_text(s_chrome.zone_label, name);
  }
}

void manifest_ui_set_status(bool online) {
  if (s_chrome.status_dot) {
    lv_obj_set_style_bg_color(
        s_chrome.status_dot, online ? COLOR_STATUS_GREEN : COLOR_STATUS_RED, 0);
  }
}

void manifest_ui_set_message(const char *msg) {
  if (!s_chrome.status_bar)
    return;
  if (msg && msg[0]) {
    lv_label_set_text(s_chrome.status_bar, msg);
    lv_obj_set_style_bg_opa(s_chrome.status_bar, LV_OPA_90, 0);
  } else {
    lv_label_set_text(s_chrome.status_bar, "");
    lv_obj_set_style_bg_opa(s_chrome.status_bar, LV_OPA_TRANSP, 0);
  }
}

void manifest_ui_set_artwork(const char *image_key) {
  // Delegate to existing ui_set_artwork for JPEG decode.
  // The artwork image pointer is in s_media.artwork_image.
  // TODO: Implement standalone artwork loading for manifest UI.
  // For now, reuse ui_set_artwork from ui.c via extern.
  (void)image_key;
}

void manifest_ui_show_volume_change(float vol, float vol_step) {
  // Optimistic volume update — apply immediately without waiting for poll
  char vol_text[16];
  format_volume_text(vol_text, sizeof(vol_text), vol, -80.0f, vol_step);
  if (s_media.volume_label) {
    lv_label_set_text(s_media.volume_label, vol_text);
  }
}

void manifest_ui_set_network_status(const char *status) {
  // TODO: Implement persistent network banner
  (void)status;
}

// Zone picker delegates — for now these are stubs.
// The zone picker is an overlay, not a manifest screen.
// It will be wired once the basic screen flow is validated.

void manifest_ui_show_zone_picker(void) {
  // Navigate to the zones list screen
  for (int i = 0; i < s_mgr.manifest.nav.count; i++) {
    int idx = find_screen_index(s_mgr.manifest.nav.order[i]);
    if (idx >= 0 && s_mgr.manifest.screens[idx].type == SCREEN_TYPE_LIST) {
      show_screen(i);
      return;
    }
  }
}

void manifest_ui_hide_zone_picker(void) {
  // Navigate back to default screen
  int def = find_screen_index(s_mgr.manifest.nav.default_screen);
  if (def >= 0) {
    // Find nav index for this screen
    for (int i = 0; i < s_mgr.manifest.nav.count; i++) {
      if (find_screen_index(s_mgr.manifest.nav.order[i]) == def) {
        show_screen(i);
        return;
      }
    }
  }
  show_screen(0);
}

bool manifest_ui_is_zone_picker_visible(void) {
  return manifest_ui_current_screen_type() == SCREEN_TYPE_LIST;
}

void manifest_ui_zone_picker_scroll(int delta) {
  s_list.selected += delta;
  if (s_list.selected < 0)
    s_list.selected = 0;
  // Clamp handled at render time
}

void manifest_ui_zone_picker_get_selected_id(char *out, size_t len) {
  if (!s_mgr.has_manifest || !out || len == 0)
    return;
  out[0] = '\0';

  // Find the list screen
  for (int i = 0; i < s_mgr.manifest.screen_count; i++) {
    if (s_mgr.manifest.screens[i].type == SCREEN_TYPE_LIST) {
      const manifest_list_t *list = &s_mgr.manifest.screens[i].data.list;
      if (s_list.selected >= 0 && s_list.selected < list->item_count) {
        strncpy(out, list->items[s_list.selected].id, len - 1);
        out[len - 1] = '\0';
      }
      return;
    }
  }
}

bool manifest_ui_zone_picker_is_current_selection(void) {
  // In manifest UI, zones are a list screen. We don't track the "current"
  // zone index internally — bridge_client handles zone identity via zone_id.
  // Return false to always allow selection (bridge_client does its own check).
  return false;
}
