#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
static lv_obj_t *s_play_icon;
static lv_obj_t *s_zone_label;
static lv_obj_t *s_message_label;
static lv_obj_t *s_play_button;
static lv_obj_t *s_vol_down_button;
static lv_obj_t *s_vol_up_button;

static pthread_mutex_t s_state_lock = PTHREAD_MUTEX_INITIALIZER;
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
    pthread_mutex_lock(&s_state_lock);
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
    pthread_mutex_unlock(&s_state_lock);
}

void ui_set_status(bool online) {
    pthread_mutex_lock(&s_state_lock);
    s_pending.online = online;
    s_dirty = true;
    pthread_mutex_unlock(&s_state_lock);
}

static void poll_pending(lv_timer_t *timer) {
    (void)timer;
    struct ui_state local;
    bool update = false;

    pthread_mutex_lock(&s_state_lock);
    if (s_dirty) {
        local = s_pending;
        s_dirty = false;
        update = true;
    }
    pthread_mutex_unlock(&s_state_lock);

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

    s_message_label = lv_label_create(dial);
    lv_obj_remove_style_all(s_message_label);
    lv_obj_set_style_text_color(s_message_label, lv_color_hex(0xaeb6d5), 0);
    lv_obj_set_style_text_font(s_message_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_message_label, LV_ALIGN_TOP_MID, 0, 36);
    lv_label_set_text(s_message_label, "Starting...");

    s_label_line1 = lv_label_create(dial);
    lv_obj_set_width(s_label_line1, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line1, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_label_line1, &lv_font_montserrat_28, 0);
    lv_label_set_long_mode(s_label_line1, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_label_line1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_label_line1, LV_ALIGN_TOP_MID, 0, 24);

    s_label_line2 = lv_label_create(dial);
    lv_obj_set_width(s_label_line2, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line2, lv_color_hex(0xaeb6d5), 0);
    lv_obj_set_style_text_font(s_label_line2, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(s_label_line2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_label_line2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_label_line2, s_label_line1, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    s_volume_bar = lv_bar_create(dial);
    lv_obj_set_size(s_volume_bar, SAFE_SIZE - 60, 12);
    lv_obj_align(s_volume_bar, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_bar_set_range(s_volume_bar, 0, 100);

    s_play_icon = lv_label_create(dial);
    lv_obj_set_style_text_color(s_play_icon, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_play_icon, &lv_font_montserrat_28, 0);
    lv_obj_align(s_play_icon, LV_ALIGN_BOTTOM_LEFT, 24, -28);

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
    lv_obj_t *play_lbl = lv_label_create(s_play_button);
    lv_label_set_text(play_lbl, LV_SYMBOL_PLAY);
    lv_obj_center(play_lbl);
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
    lv_label_set_text(s_play_icon, state->playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
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
            case LV_KEY_LEFT:
                s_input_cb(UI_INPUT_VOL_DOWN);
                break;
            case LV_KEY_RIGHT:
                s_input_cb(UI_INPUT_VOL_UP);
                break;
            case LV_KEY_ENTER:
            case ' ':
                s_input_cb(UI_INPUT_PLAY_PAUSE);
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
