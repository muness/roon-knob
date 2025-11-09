#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os_mutex.h"

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"

#include "ui.h"

#define SCREEN_SIZE 240
#define SAFE_SIZE 220

struct ui_state {
    char line1[128];
    char line2[128];
    bool playing;
    int volume;
    bool online;
};

#include <time.h>

static lv_display_t *s_display;
static lv_obj_t *s_label_line1;
static lv_obj_t *s_label_line2;
static lv_obj_t *s_status_dot;
static lv_obj_t *s_volume_bar;
static lv_obj_t *s_zone_label;
static lv_obj_t *s_message_label;
static lv_obj_t *s_play_button;
static lv_obj_t *s_play_button_label;
static lv_obj_t *s_vol_down_button;
static lv_obj_t *s_vol_up_button;

static lv_obj_t *s_zone_picker_container;
static lv_obj_t *s_zone_list;
static bool s_zone_picker_visible = false;
static int s_zone_picker_selected = 0;

static os_mutex_t s_state_lock = OS_MUTEX_INITIALIZER;
static struct ui_state s_pending = {
    .line1 = "Waiting for bridge",
    .line2 = "",
    .playing = false,
    .volume = 0,
    .online = false,
};
static bool s_dirty = true;
static lv_indev_t *s_keyboard;
static lv_group_t *s_key_group;
static ui_input_cb_t s_input_cb;
static char s_zone_name[64] = "Zone";

static void apply_state(const struct ui_state *state);
static void build_layout(void);
static void poll_pending(lv_timer_t *timer);
static void set_status_dot(bool online);
static void keyboard_event_cb(lv_event_t *e);

void ui_init(void) {
    lv_init();

    s_display = lv_sdl_window_create(SCREEN_SIZE, SCREEN_SIZE);
    lv_display_set_default(s_display);
    lv_sdl_mouse_create();

    build_layout();
    lv_timer_create(poll_pending, 60, NULL);

    s_keyboard = lv_sdl_keyboard_create();
    if(s_keyboard) {
        lv_obj_t *screen = lv_screen_active();
        s_key_group = lv_group_create();
        lv_group_add_obj(s_key_group, screen);
        lv_group_focus_obj(screen);
        lv_indev_set_group(s_keyboard, s_key_group);
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_event_cb(screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    }

    lv_label_set_text(s_zone_label, s_zone_name);
    lv_label_set_text(s_label_line1, s_pending.line1);
}

void ui_update(const char *line1, const char *line2, bool playing, int volume) {
    os_mutex_lock(&s_state_lock);
    if (line1) {
        snprintf(s_pending.line1, sizeof(s_pending.line1), "%s", line1);
    }
    if (line2) {
        snprintf(s_pending.line2, sizeof(s_pending.line2), "%s", line2);
    }
    s_pending.playing = playing;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_pending.volume = volume;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_set_status(bool online) {
    os_mutex_lock(&s_state_lock);
    s_pending.online = online;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

static void poll_pending(lv_timer_t *timer) {
    (void)timer;
    struct ui_state local;
    bool update = false;

    os_mutex_lock(&s_state_lock);
    if (s_dirty) {
        local = s_pending;
        s_dirty = false;
        update = true;
    }
    os_mutex_unlock(&s_state_lock);

    if (update) {
        apply_state(&local);
    }
}

static void build_layout(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x04050a), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *dial = lv_obj_create(screen);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, SAFE_SIZE, SAFE_SIZE);
    lv_obj_set_style_bg_color(dial, lv_color_hex(0x11131b), 0);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dial, SAFE_SIZE / 2, 0);
    lv_obj_center(dial);

    s_status_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(s_status_dot);
    lv_obj_set_size(s_status_dot, 14, 14);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0x5b5f73), 0);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -16, 16);

    s_zone_label = lv_label_create(dial);
    lv_obj_remove_style_all(s_zone_label);
    lv_label_set_text(s_zone_label, s_zone_name);
    lv_obj_set_style_text_font(s_zone_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_zone_label, lv_color_hex(0xaeb6d5), 0);
    lv_obj_align(s_zone_label, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_add_flag(s_zone_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_zone_label, keyboard_event_cb, LV_EVENT_CLICKED, (void *)UI_INPUT_MENU);

    s_message_label = lv_label_create(dial);
    lv_obj_remove_style_all(s_message_label);
    lv_obj_set_style_text_color(s_message_label, lv_color_hex(0xaeb6d5), 0);
    lv_obj_set_style_text_font(s_message_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_message_label, LV_ALIGN_TOP_MID, 0, 36);
    lv_label_set_text(s_message_label, "Starting...");

    s_label_line1 = lv_label_create(dial);
    lv_obj_set_width(s_label_line1, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line1, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_label_line1, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(s_label_line1, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_label_line1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_label_line1, LV_ALIGN_TOP_MID, 0, 24);

    s_label_line2 = lv_label_create(dial);
    lv_obj_set_width(s_label_line2, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line2, lv_color_hex(0xaeb6d5), 0);
    lv_obj_set_style_text_font(s_label_line2, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_label_line2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_label_line2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_label_line2, s_label_line1, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    s_volume_bar = lv_bar_create(dial);
    lv_obj_set_size(s_volume_bar, SAFE_SIZE - 60, 12);
    lv_obj_align(s_volume_bar, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_bar_set_range(s_volume_bar, 0, 100);

    s_vol_down_button = lv_btn_create(dial);
    lv_obj_set_size(s_vol_down_button, 40, 40);
    lv_obj_align(s_vol_down_button, LV_ALIGN_BOTTOM_LEFT, 36, -70);
    lv_obj_t *down_label = lv_label_create(s_vol_down_button);
    lv_label_set_text(down_label, "-");
    lv_obj_center(down_label);
    lv_obj_add_event_cb(s_vol_down_button, keyboard_event_cb, LV_EVENT_CLICKED, (void *)UI_INPUT_VOL_DOWN);

    s_play_button = lv_btn_create(dial);
    lv_obj_set_size(s_play_button, 48, 40);
    lv_obj_align(s_play_button, LV_ALIGN_BOTTOM_MID, 0, -70);
    s_play_button_label = lv_label_create(s_play_button);
    lv_label_set_text(s_play_button_label, LV_SYMBOL_PLAY);
    lv_obj_center(s_play_button_label);
    lv_obj_add_event_cb(s_play_button, keyboard_event_cb, LV_EVENT_CLICKED, (void *)UI_INPUT_PLAY_PAUSE);

    s_vol_up_button = lv_btn_create(dial);
    lv_obj_set_size(s_vol_up_button, 40, 40);
    lv_obj_align(s_vol_up_button, LV_ALIGN_BOTTOM_RIGHT, -36, -70);
    lv_obj_t *up_label = lv_label_create(s_vol_up_button);
    lv_label_set_text(up_label, "+");
    lv_obj_center(up_label);
    lv_obj_add_event_cb(s_vol_up_button, keyboard_event_cb, LV_EVENT_CLICKED, (void *)UI_INPUT_VOL_UP);

    apply_state(&s_pending);
}

static void apply_state(const struct ui_state *state) {
    lv_label_set_text(s_label_line1, state->line1);
    lv_label_set_text(s_label_line2, state->line2);
    lv_bar_set_value(s_volume_bar, state->volume, LV_ANIM_OFF);
    if (s_play_button_label) {
        const char *icon = state->playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
        lv_label_set_text(s_play_button_label, icon);
    }
    set_status_dot(state->online);
}

static void set_status_dot(bool online) {
    lv_color_t color = online ? lv_color_hex(0x41db64) : lv_color_hex(0xdb4154);
    lv_obj_set_style_bg_color(s_status_dot, color, 0);
}

void ui_set_input_handler(ui_input_cb_t handler) {
    s_input_cb = handler;
}

void ui_set_zone_name(const char *zone_name) {
    if (!zone_name || !s_zone_label) return;
    lv_label_set_text(s_zone_label, zone_name);
    snprintf(s_zone_name, sizeof(s_zone_name), "%s", zone_name);
}

void ui_set_message(const char *msg) {
    if (!msg || !s_message_label) return;
    lv_label_set_text(s_message_label, msg);
}

static void keyboard_event_cb(lv_event_t *e) {
    if (!s_input_cb) return;
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        switch(key) {
            case LV_KEY_UP:
            case LV_KEY_RIGHT:
                s_input_cb(UI_INPUT_VOL_UP);
                break;
            case LV_KEY_DOWN:
            case LV_KEY_LEFT:
                s_input_cb(UI_INPUT_VOL_DOWN);
                break;
            case LV_KEY_ENTER:
            case ' ':
                s_input_cb(UI_INPUT_PLAY_PAUSE);
                break;
            case 'z':
            case 'm':
                s_input_cb(UI_INPUT_MENU);
                break;
            default:
                break;
        }
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_input_event_t action = (ui_input_event_t)(intptr_t)lv_event_get_user_data(e);
        s_input_cb(action);
    }
}

void ui_loop_iter(void) {
    lv_tick_inc(5);
    lv_timer_handler();
}

void ui_show_zone_picker(const char **zone_names, int zone_count, int selected_idx) {
    if (s_zone_picker_visible) return;

    s_zone_picker_selected = selected_idx;
    s_zone_picker_visible = true;

    // Create container overlay
    s_zone_picker_container = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_zone_picker_container);
    lv_obj_set_size(s_zone_picker_container, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(s_zone_picker_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_zone_picker_container, LV_OPA_90, 0);
    lv_obj_center(s_zone_picker_container);

    // Create zone list container
    lv_obj_t *list_bg = lv_obj_create(s_zone_picker_container);
    lv_obj_remove_style_all(list_bg);
    lv_obj_set_size(list_bg, SAFE_SIZE - 20, SAFE_SIZE - 40);
    lv_obj_set_style_bg_color(list_bg, lv_color_hex(0x1a1c24), 0);
    lv_obj_set_style_bg_opa(list_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list_bg, 12, 0);
    lv_obj_set_style_border_width(list_bg, 2, 0);
    lv_obj_set_style_border_color(list_bg, lv_color_hex(0x3a3c44), 0);
    lv_obj_center(list_bg);

    // Title
    lv_obj_t *title = lv_label_create(list_bg);
    lv_label_set_text(title, "Select Zone");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Zone list
    s_zone_list = lv_list_create(list_bg);
    lv_obj_set_size(s_zone_list, SAFE_SIZE - 40, SAFE_SIZE - 80);
    lv_obj_align(s_zone_list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_zone_list, lv_color_hex(0x11131b), 0);
    lv_obj_set_style_border_width(s_zone_list, 0, 0);

    for (int i = 0; i < zone_count; i++) {
        lv_obj_t *btn = lv_list_add_button(s_zone_list, NULL, zone_names[i]);
        lv_obj_set_style_bg_color(btn, lv_color_hex(i == selected_idx ? 0x2a5a9a : 0x11131b), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xffffff), 0);
    }
}

void ui_hide_zone_picker(void) {
    if (!s_zone_picker_visible) return;

    if (s_zone_picker_container) {
        lv_obj_del(s_zone_picker_container);
        s_zone_picker_container = NULL;
        s_zone_list = NULL;
    }
    s_zone_picker_visible = false;
}

bool ui_is_zone_picker_visible(void) {
    return s_zone_picker_visible;
}

int ui_zone_picker_get_selected(void) {
    return s_zone_picker_selected;
}

void ui_zone_picker_scroll(int delta) {
    if (!s_zone_picker_visible || !s_zone_list) return;

    uint32_t child_count = lv_obj_get_child_count(s_zone_list);
    if (child_count == 0) return;

    int new_selected = s_zone_picker_selected + delta;
    if (new_selected < 0) new_selected = 0;
    if (new_selected >= (int)child_count) new_selected = child_count - 1;

    if (new_selected != s_zone_picker_selected) {
        // Update old button
        lv_obj_t *old_btn = lv_obj_get_child(s_zone_list, s_zone_picker_selected);
        if (old_btn) {
            lv_obj_set_style_bg_color(old_btn, lv_color_hex(0x11131b), 0);
        }

        // Update new button
        s_zone_picker_selected = new_selected;
        lv_obj_t *new_btn = lv_obj_get_child(s_zone_list, s_zone_picker_selected);
        if (new_btn) {
            lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x2a5a9a), 0);
            lv_obj_scroll_to_view(new_btn, LV_ANIM_ON);
        }
    }
}
