#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lvgl.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"

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

static lv_display_t *s_display;
static lv_obj_t *s_label_line1;
static lv_obj_t *s_label_line2;
static lv_obj_t *s_status_dot;
static lv_obj_t *s_volume_bar;
static lv_obj_t *s_play_icon;
static pthread_t s_lv_thread;
static pthread_mutex_t s_state_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ui_state s_pending = {
    .line1 = "Waiting for bridge",
    .line2 = "",
    .playing = false,
    .volume = 0,
    .online = false,
};
static bool s_dirty = true;
static bool s_running = true;

static void apply_state(const struct ui_state *state);
static void *lvgl_thread(void *arg);
static void build_layout(void);
static void poll_pending(lv_timer_t *timer);
static void set_status_dot(bool online);

void ui_init(void) {
    lv_init();

    s_display = lv_sdl_window_create(SCREEN_SIZE, SCREEN_SIZE);
    lv_display_set_default(s_display);
    lv_sdl_mouse_create();

    build_layout();
    lv_timer_create(poll_pending, 60, NULL);

    pthread_create(&s_lv_thread, NULL, lvgl_thread, NULL);
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

static void *lvgl_thread(void *arg) {
    (void)arg;
    while (s_running) {
        lv_tick_inc(5);
        lv_timer_handler();
        usleep(5000);
    }
    return NULL;
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

    s_label_line1 = lv_label_create(dial);
    lv_obj_set_width(s_label_line1, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line1, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_label_line1, &lv_font_montserrat_28, 0);
    lv_label_set_long_mode(s_label_line1, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_label_line1, LV_ALIGN_TOP_MID, 0, 24);

    s_label_line2 = lv_label_create(dial);
    lv_obj_set_width(s_label_line2, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line2, lv_color_hex(0xaeb6d5), 0);
    lv_obj_set_style_text_font(s_label_line2, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(s_label_line2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align_to(s_label_line2, s_label_line1, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    s_volume_bar = lv_bar_create(dial);
    lv_obj_set_size(s_volume_bar, SAFE_SIZE - 60, 12);
    lv_obj_align(s_volume_bar, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_bar_set_range(s_volume_bar, 0, 100);

    s_play_icon = lv_label_create(dial);
    lv_obj_set_style_text_color(s_play_icon, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_play_icon, &lv_font_montserrat_28, 0);
    lv_obj_align(s_play_icon, LV_ALIGN_BOTTOM_LEFT, 24, -28);

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
