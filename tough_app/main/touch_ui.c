// touch_ui.h implementation for M5Stack Tough's 320x240 ILI9342C screen.
// Reduced-scope LVGL renderer: now-playing text, play/pause/prev/next,
// +/- volume buttons (no rotary encoder on this target), a touch-scrollable
// zone list, a network status banner, and a minimal settings screen. See
// touch_ui.h for the full scope note.

#include "touch_ui.h"

#include "controller_action.h"
#include "controller_config.h"
#include "controller_input.h"
#include "platform/platform_task.h"
#include "wifi_manager.h"

#include "lvgl.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "touch_ui";

#define SCREEN_W 320
#define SCREEN_H 240
#define MAX_ZONE_PICKER_ZONES 32
#define MAX_ZONE_ID_LEN 64

// ── Widgets ──────────────────────────────────────────────────────────────

static lv_obj_t *s_zone_label;
static lv_obj_t *s_status_dot;
static lv_obj_t *s_track_label;
static lv_obj_t *s_artist_label;
static lv_obj_t *s_album_label;
static lv_obj_t *s_volume_label;
static lv_obj_t *s_play_icon;
static lv_obj_t *s_message_label;
static lv_obj_t *s_network_banner;

static lv_timer_t *s_message_clear_timer;

static bool s_playing = false;

// Zone picker state
static lv_obj_t *s_zone_picker_overlay;
static lv_obj_t *s_zone_list;
static bool s_zone_picker_visible = false;
static int s_zone_picker_count = 0;
static int s_zone_picker_selected = -1;
static int s_zone_picker_current = -1;
static char s_zone_picker_ids[MAX_ZONE_PICKER_ZONES][MAX_ZONE_ID_LEN];

// Settings screen state
static lv_obj_t *s_settings_overlay;
static bool s_zone_long_pressed = false;

// ── Helpers ──────────────────────────────────────────────────────────────

static void format_volume_text(char *out, size_t len, float volume,
                               float volume_step) {
    if (volume_step > 0.0f && volume_step < 1.0f) {
        snprintf(out, len, "%.1f", (double)volume);
    } else {
        snprintf(out, len, "%.0f", (double)volume);
    }
}

static void dispatch_command(controller_command_kind_t kind) {
    controller_action_t action = controller_action_command(
        controller_command_make(kind));
    (void)controller_input_dispatch_action(&action);
}

static void dispatch_simple(controller_action_kind_t kind) {
    controller_action_t action = controller_action_simple(kind);
    (void)controller_input_dispatch_action(&action);
}

// ── Event callbacks ──────────────────────────────────────────────────────

static void zone_label_event_cb(lv_event_t *e) {
    (void)e;
    if (s_zone_long_pressed) {
        s_zone_long_pressed = false;
        return;
    }
    dispatch_simple(CONTROLLER_ACTION_OPEN_ZONE_PICKER);
}

static void zone_label_long_press_cb(lv_event_t *e) {
    (void)e;
    s_zone_long_pressed = true;
    touch_ui_show_settings();
}

static void btn_prev_event_cb(lv_event_t *e) {
    (void)e;
    dispatch_command(CONTROLLER_COMMAND_PREVIOUS_TRACK);
}

static void btn_play_event_cb(lv_event_t *e) {
    (void)e;
    dispatch_command(CONTROLLER_COMMAND_TOGGLE_PLAYBACK);
}

static void btn_next_event_cb(lv_event_t *e) {
    (void)e;
    dispatch_command(CONTROLLER_COMMAND_NEXT_TRACK);
}

static void btn_vol_down_event_cb(lv_event_t *e) {
    (void)e;
    controller_action_t action = controller_action_command(
        controller_command_adjust_volume(-1));
    (void)controller_input_dispatch_action(&action);
}

static void btn_vol_up_event_cb(lv_event_t *e) {
    (void)e;
    controller_action_t action = controller_action_command(
        controller_command_adjust_volume(1));
    (void)controller_input_dispatch_action(&action);
}

static void zone_list_item_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);
    s_zone_picker_selected = index;
    dispatch_simple(CONTROLLER_ACTION_SELECT_ZONE_PICKER);
}

static void settings_close_event_cb(lv_event_t *e) {
    (void)e;
    if (s_settings_overlay) {
        lv_obj_delete(s_settings_overlay);
        s_settings_overlay = NULL;
    }
}

static void settings_forget_wifi_event_cb(lv_event_t *e) {
    (void)e;
    wifi_mgr_forget_wifi();  // Reboots the device; no need to close overlay.
}

static void message_clear_timer_cb(lv_timer_t *timer) {
    (void)timer;
    s_message_clear_timer = NULL;
    if (s_message_label) {
        lv_obj_add_flag(s_message_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Init / layout ────────────────────────────────────────────────────────

void touch_ui_init(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101018), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // Header: zone name (tap -> zone picker, long-press -> settings) + status dot.
    s_zone_label = lv_label_create(screen);
    lv_label_set_text(s_zone_label, "No Zone");
    lv_obj_set_style_text_color(s_zone_label, lv_color_hex(0xfafafa), 0);
    lv_obj_align(s_zone_label, LV_ALIGN_TOP_LEFT, 10, 6);
    lv_obj_add_flag(s_zone_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_zone_label, zone_label_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_zone_label, zone_label_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    s_status_dot = lv_obj_create(screen);
    lv_obj_set_size(s_status_dot, 10, 10);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0x666666), 0);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -10, 10);

    // Now-playing text (three lines, all genuinely rendered -- unlike Dial's
    // common/ui.c which ignores a third line; see touch_ui.h).
    s_track_label = lv_label_create(screen);
    lv_label_set_long_mode(s_track_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_track_label, SCREEN_W - 20);
    lv_obj_set_style_text_font(s_track_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_track_label, lv_color_hex(0xfafafa), 0);
    lv_label_set_text(s_track_label, "");
    lv_obj_align(s_track_label, LV_ALIGN_TOP_MID, 0, 40);

    s_artist_label = lv_label_create(screen);
    lv_label_set_long_mode(s_artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_artist_label, SCREEN_W - 20);
    lv_obj_set_style_text_font(s_artist_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_artist_label, lv_color_hex(0xaaaaaa), 0);
    lv_label_set_text(s_artist_label, "");
    lv_obj_align(s_artist_label, LV_ALIGN_TOP_MID, 0, 66);

    s_album_label = lv_label_create(screen);
    lv_label_set_long_mode(s_album_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_album_label, SCREEN_W - 20);
    lv_obj_set_style_text_font(s_album_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_album_label, lv_color_hex(0x888888), 0);
    lv_label_set_text(s_album_label, "");
    lv_obj_align(s_album_label, LV_ALIGN_TOP_MID, 0, 86);

    // Volume row.
    lv_obj_t *btn_vol_down = lv_button_create(screen);
    lv_obj_set_size(btn_vol_down, 44, 44);
    lv_obj_align(btn_vol_down, LV_ALIGN_CENTER, -70, 20);
    lv_obj_add_event_cb(btn_vol_down, btn_vol_down_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_down_icon = lv_label_create(btn_vol_down);
    lv_label_set_text(vol_down_icon, LV_SYMBOL_MINUS);
    lv_obj_center(vol_down_icon);

    s_volume_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_volume_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_volume_label, lv_color_hex(0xfafafa), 0);
    lv_label_set_text(s_volume_label, "--");
    lv_obj_align(s_volume_label, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t *btn_vol_up = lv_button_create(screen);
    lv_obj_set_size(btn_vol_up, 44, 44);
    lv_obj_align(btn_vol_up, LV_ALIGN_CENTER, 70, 20);
    lv_obj_add_event_cb(btn_vol_up, btn_vol_up_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_up_icon = lv_label_create(btn_vol_up);
    lv_label_set_text(vol_up_icon, LV_SYMBOL_PLUS);
    lv_obj_center(vol_up_icon);

    // Transport row.
    lv_obj_t *btn_prev = lv_button_create(screen);
    lv_obj_set_size(btn_prev, 50, 50);
    lv_obj_align(btn_prev, LV_ALIGN_BOTTOM_MID, -80, -16);
    lv_obj_add_event_cb(btn_prev, btn_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *prev_icon = lv_label_create(btn_prev);
    lv_label_set_text(prev_icon, LV_SYMBOL_PREV);
    lv_obj_center(prev_icon);

    lv_obj_t *btn_play = lv_button_create(screen);
    lv_obj_set_size(btn_play, 60, 60);
    lv_obj_align(btn_play, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(btn_play, btn_play_event_cb, LV_EVENT_CLICKED, NULL);
    s_play_icon = lv_label_create(btn_play);
    lv_label_set_text(s_play_icon, LV_SYMBOL_PLAY);
    lv_obj_center(s_play_icon);

    lv_obj_t *btn_next = lv_button_create(screen);
    lv_obj_set_size(btn_next, 50, 50);
    lv_obj_align(btn_next, LV_ALIGN_BOTTOM_MID, 80, -16);
    lv_obj_add_event_cb(btn_next, btn_next_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_icon = lv_label_create(btn_next);
    lv_label_set_text(next_icon, LV_SYMBOL_NEXT);
    lv_obj_center(next_icon);

    // Transient message toast (hidden by default).
    s_message_label = lv_label_create(screen);
    lv_obj_set_width(s_message_label, SCREEN_W - 20);
    lv_label_set_long_mode(s_message_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_message_label, lv_color_hex(0xffd54f), 0);
    lv_obj_align(s_message_label, LV_ALIGN_TOP_MID, 0, 110);
    lv_label_set_text(s_message_label, "");
    lv_obj_add_flag(s_message_label, LV_OBJ_FLAG_HIDDEN);

    // Persistent network status banner at the very bottom (hidden when empty).
    s_network_banner = lv_label_create(screen);
    lv_obj_set_width(s_network_banner, SCREEN_W - 20);
    lv_label_set_long_mode(s_network_banner, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_network_banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_network_banner, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_network_banner, lv_color_hex(0xff8a65), 0);
    lv_obj_align(s_network_banner, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(s_network_banner, "");
    lv_obj_add_flag(s_network_banner, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "touch_ui initialized");
}

void touch_ui_process(void) {
    lv_timer_handler();
}

void touch_ui_set_status(bool online) {
    if (!s_status_dot) return;
    lv_obj_set_style_bg_color(s_status_dot,
        online ? lv_color_hex(0x00c853) : lv_color_hex(0x666666), 0);
}

static void clear_message_now(void) {
    if (s_message_clear_timer) {
        lv_timer_delete(s_message_clear_timer);
        s_message_clear_timer = NULL;
    }
}

void touch_ui_set_message(const char *msg) {
    if (!s_message_label) return;
    if (!msg || !msg[0]) {
        lv_obj_add_flag(s_message_label, LV_OBJ_FLAG_HIDDEN);
        clear_message_now();
        return;
    }
    lv_label_set_text(s_message_label, msg);
    lv_obj_clear_flag(s_message_label, LV_OBJ_FLAG_HIDDEN);
    clear_message_now();
    s_message_clear_timer = lv_timer_create(message_clear_timer_cb, 4000, NULL);
    lv_timer_set_repeat_count(s_message_clear_timer, 1);
}

void touch_ui_set_zone_name(const char *zone_name) {
    if (!s_zone_label) return;
    lv_label_set_text(s_zone_label, (zone_name && zone_name[0]) ? zone_name : "No Zone");
}

void touch_ui_set_network_status(const char *status) {
    if (!s_network_banner) return;
    if (!status || !status[0]) {
        lv_label_set_text(s_network_banner, "");
        lv_obj_add_flag(s_network_banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(s_network_banner, status);
    lv_obj_clear_flag(s_network_banner, LV_OBJ_FLAG_HIDDEN);
}

static void set_zone_name_on_ui(void *arg) {
    char *name = arg;
    touch_ui_set_zone_name(name);
    free(name);
}

void touch_ui_post_zone_name(const char *name) {
    char *copy = strdup(name ? name : "");
    if (!copy) return;
    if (!platform_task_post_to_ui(set_zone_name_on_ui, copy)) {
        free(copy);
    }
}

static void set_network_status_on_ui(void *arg) {
    char *status = arg;
    touch_ui_set_network_status(status);
    free(status);
}

void touch_ui_post_network_status(const char *status) {
    char *copy = strdup(status ? status : "");
    if (!copy) return;
    if (!platform_task_post_to_ui(set_network_status_on_ui, copy)) {
        free(copy);
    }
}

void touch_ui_set_artwork(const char *image_key) {
    // Alpha scope: no artwork rendering on this target (see touch_ui.h).
    // Accepted and logged so bridge_client needs no target conditionals.
    if (image_key && image_key[0]) {
        ESP_LOGD(TAG, "Artwork available (not rendered this Alpha): %s", image_key);
    }
}

void touch_ui_show_volume_change(float vol, float vol_step) {
    if (!s_volume_label) return;
    char text[16];
    format_volume_text(text, sizeof(text), vol, vol_step);
    lv_label_set_text(s_volume_label, text);
}

void touch_ui_update(const char *line1, const char *line2, const char *line3,
                     bool playing, float volume, float volume_min,
                     float volume_max, float volume_step, int seek_position,
                     int length) {
    (void)volume_min;
    (void)volume_max;
    (void)seek_position;
    (void)length;

    if (s_track_label && line1) {
        lv_label_set_text(s_track_label, line1);
    }
    if (s_artist_label && line2) {
        lv_label_set_text(s_artist_label, line2);
    }
    if (s_album_label && line3) {
        lv_label_set_text(s_album_label, line3);
    }
    if (s_play_icon && playing != s_playing) {
        s_playing = playing;
        lv_label_set_text(s_play_icon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    touch_ui_show_volume_change(volume, volume_step);
}

// ── Zone picker ──────────────────────────────────────────────────────────

void touch_ui_show_zone_picker(const char **zone_names, const char **zone_ids,
                               int zone_count, int selected_idx) {
    if (s_zone_picker_visible) return;

    s_zone_picker_count = (zone_count > MAX_ZONE_PICKER_ZONES) ? MAX_ZONE_PICKER_ZONES : zone_count;
    s_zone_picker_selected = selected_idx;
    s_zone_picker_current = selected_idx;
    for (int i = 0; i < s_zone_picker_count; i++) {
        snprintf(s_zone_picker_ids[i], MAX_ZONE_ID_LEN, "%s", zone_ids[i]);
    }

    s_zone_picker_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_zone_picker_overlay, SCREEN_W, SCREEN_H);
    lv_obj_center(s_zone_picker_overlay);
    lv_obj_set_style_bg_color(s_zone_picker_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_zone_picker_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_zone_picker_overlay, 0, 0);
    lv_obj_set_style_radius(s_zone_picker_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_zone_picker_overlay, 4, 0);

    lv_obj_t *title = lv_label_create(s_zone_picker_overlay);
    lv_label_set_text(title, "SELECT ZONE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xfafafa), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    s_zone_list = lv_list_create(s_zone_picker_overlay);
    lv_obj_set_size(s_zone_list, SCREEN_W - 16, SCREEN_H - 40);
    lv_obj_align(s_zone_list, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(s_zone_list, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(s_zone_list, 0, 0);

    for (int i = 0; i < s_zone_picker_count; i++) {
        lv_obj_t *btn = lv_list_add_btn(s_zone_list, NULL, zone_names[i]);
        lv_obj_set_style_min_height(btn, 40, 0);
        if (i == selected_idx) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a4a6a), 0);
        }
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, zone_list_item_event_cb, LV_EVENT_CLICKED, NULL);
    }

    s_zone_picker_visible = true;
}

void touch_ui_hide_zone_picker(void) {
    if (!s_zone_picker_visible) return;
    if (s_zone_picker_overlay) {
        lv_obj_delete(s_zone_picker_overlay);
        s_zone_picker_overlay = NULL;
        s_zone_list = NULL;
    }
    s_zone_picker_visible = false;
}

bool touch_ui_is_zone_picker_visible(void) {
    return s_zone_picker_visible;
}

void touch_ui_zone_picker_scroll(int delta) {
    // Touch scrolling is native to the LVGL list widget; this target has no
    // rotary encoder to drive scroll deltas from (see tough_capabilities.h).
    (void)delta;
}

void touch_ui_zone_picker_get_selected_id(char *out, size_t len) {
    if (!out || len == 0) return;
    out[0] = '\0';
    if (!s_zone_picker_visible) return;
    if (s_zone_picker_selected >= 0 && s_zone_picker_selected < s_zone_picker_count) {
        snprintf(out, len, "%s", s_zone_picker_ids[s_zone_picker_selected]);
    }
}

bool touch_ui_zone_picker_is_current_selection(void) {
    return s_zone_picker_selected == s_zone_picker_current;
}

void touch_ui_update_battery(void) {
    // Battery is not wired up this Alpha -- nothing to render (see touch_ui.h).
}

// ── Settings screen ──────────────────────────────────────────────────────

void touch_ui_show_settings(void) {
    if (s_settings_overlay) return;

    s_settings_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_settings_overlay, SCREEN_W, SCREEN_H);
    lv_obj_center(s_settings_overlay);
    lv_obj_set_style_bg_color(s_settings_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_settings_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_settings_overlay, 0, 0);
    lv_obj_set_style_radius(s_settings_overlay, 0, 0);

    lv_obj_t *title = lv_label_create(s_settings_overlay);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xfafafa), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    char info[256];
    controller_config_snapshot_t snapshot = {0};
    char ssid[33] = "(none)";
    char bridge[128] = "(mDNS auto-discovery)";
    if (controller_config_snapshot(&snapshot)) {
        if (snapshot.value.ssid[0]) {
            snprintf(ssid, sizeof(ssid), "%s", snapshot.value.ssid);
        }
        if (snapshot.value.bridge_base[0]) {
            snprintf(bridge, sizeof(bridge), "%s", snapshot.value.bridge_base);
        }
    }
    char ip[32] = "(none)";
    wifi_mgr_get_ip(ip, sizeof(ip));
    const esp_app_desc_t *app_desc = esp_app_get_description();

    snprintf(info, sizeof(info),
             "WiFi: %s\nIP: %s\nBridge: %s\nVersion: %s",
             ssid, ip, bridge, app_desc->version);

    lv_obj_t *info_label = lv_label_create(s_settings_overlay);
    lv_label_set_text(info_label, info);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xcccccc), 0);
    lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 12, 50);

    lv_obj_t *btn_forget = lv_button_create(s_settings_overlay);
    lv_obj_align(btn_forget, LV_ALIGN_BOTTOM_MID, -60, -14);
    lv_obj_add_event_cb(btn_forget, settings_forget_wifi_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *forget_label = lv_label_create(btn_forget);
    lv_label_set_text(forget_label, "Forget WiFi");

    lv_obj_t *btn_close = lv_button_create(s_settings_overlay);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 70, -14);
    lv_obj_add_event_cb(btn_close, settings_close_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(btn_close);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE " Close");
}
