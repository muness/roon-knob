#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "os_mutex.h"
#include "platform/platform_task.h"
#include "lvgl.h"
#include "ui.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define UI_TAG "ui"
#else
#define UI_TAG "ui"
#define ESP_LOGI(tag, fmt, ...) printf("[I] " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W] " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E] " tag ": " fmt "\n", ##__VA_ARGS__)
#endif

#ifdef ESP_PLATFORM
#define SCREEN_SIZE 360
#define SAFE_SIZE 340
#else
#define SCREEN_SIZE 240
#define SAFE_SIZE 220
#endif

struct ui_state {
    char line1[128];
    char line2[128];
    bool playing;
    int volume;
    bool online;
    int seek_position;
    int length;
};

static lv_obj_t *s_label_line1;
static lv_obj_t *s_label_line2;
static lv_obj_t *s_paused_label;
static lv_obj_t *s_status_dot;
static lv_obj_t *s_volume_bar;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_zone_label;
static lv_obj_t *s_message_overlay;
static lv_obj_t *s_message_label;
static lv_timer_t *s_message_timer;

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
    .seek_position = 0,
    .length = 0,
};
static bool s_dirty = true;
static char s_pending_message[128] = "";
static bool s_message_dirty = false;
static ui_input_cb_t s_input_cb;
static char s_zone_name[64] = "Zone";

static inline const lv_font_t *font_small(void) { return LV_FONT_DEFAULT; }
static inline const lv_font_t *font_normal(void) { return LV_FONT_DEFAULT; }
static inline const lv_font_t *font_large(void) { return LV_FONT_DEFAULT; }

static void apply_state(const struct ui_state *state);
static void build_layout(void);
static void poll_pending(lv_timer_t *timer);
static void set_status_dot(bool online);
static void zone_label_event_cb(lv_event_t *e);
static void zone_button_event_cb(lv_event_t *e);
static void show_message_overlay(const char *msg);
static void hide_message_overlay(lv_timer_t *timer);

void ui_init(void) {
    ESP_LOGI(UI_TAG, "ui_init: start");

    // Apply LVGL default dark theme for better contrast
    lv_theme_t *theme = lv_theme_default_init(
        NULL,                          // display
        lv_palette_main(LV_PALETTE_BLUE),  // primary color
        lv_palette_main(LV_PALETTE_RED),   // secondary color
        true,                          // dark mode
        LV_FONT_DEFAULT                // font
    );
    if (theme) {
        lv_display_set_theme(lv_display_get_default(), theme);
        ESP_LOGI(UI_TAG, "Applied default dark theme");
    }

    build_layout();

    ESP_LOGI(UI_TAG, "build_layout done");

    lv_timer_create(poll_pending, 60, NULL);

    lv_label_set_text(s_zone_label, s_zone_name);
    lv_label_set_text(s_label_line1, s_pending.line1);

    // Set initial message via the safe mechanism
    ui_set_message("Starting...");

    ESP_LOGI(UI_TAG, "ui_init: done");
}

void ui_update(const char *line1, const char *line2, bool playing, int volume, int seek_position, int length) {
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
    s_pending.seek_position = seek_position > 0 ? seek_position : 0;
    s_pending.length = length > 0 ? length : 0;
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
    char message[128] = "";
    bool show_message = false;

    os_mutex_lock(&s_state_lock);
    if (s_dirty) {
        local = s_pending;
        s_dirty = false;
        update = true;
    }
    if (s_message_dirty) {
        snprintf(message, sizeof(message), "%s", s_pending_message);
        s_message_dirty = false;
        show_message = true;
    }
    os_mutex_unlock(&s_state_lock);

    if (update) {
        apply_state(&local);
    }
    if (show_message) {
        show_message_overlay(message);
    }
}

static void build_layout(void) {
    ESP_LOGI(UI_TAG, "build_layout: getting active screen");
    lv_obj_t *screen = lv_screen_active();
    if (!screen) {
        ESP_LOGE(UI_TAG, "lv_screen_active returned NULL - display driver not registered!");
        return;
    }

    ESP_LOGI(UI_TAG, "build_layout: setting up screen styles");
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);  // Pure black background
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *dial = lv_obj_create(screen);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, SAFE_SIZE, SAFE_SIZE);
    lv_obj_set_style_bg_color(dial, lv_color_hex(0x000000), 0);  // Pure black for better contrast
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dial, SAFE_SIZE / 2, 0);
    lv_obj_center(dial);

    s_status_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(s_status_dot);
    lv_obj_set_size(s_status_dot, 14, 14);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0x808080), 0);  // Gray offline, will change when online
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -16, 16);

    s_zone_label = lv_label_create(dial);
    lv_obj_remove_style_all(s_zone_label);
    lv_label_set_text(s_zone_label, s_zone_name);
    lv_obj_set_style_text_font(s_zone_label, font_small(), 0);
    lv_obj_set_style_text_color(s_zone_label, lv_color_hex(0xaeb6d5), 0);
    lv_obj_align(s_zone_label, LV_ALIGN_TOP_MID, 0, 25);  // Lower to avoid circular clip
    lv_obj_add_flag(s_zone_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_zone_label, zone_label_event_cb, LV_EVENT_CLICKED, NULL);

    s_label_line1 = lv_label_create(dial);
    lv_obj_set_width(s_label_line1, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line1, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_label_line1, font_large(), 0);
    lv_label_set_long_mode(s_label_line1, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_label_line1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_label_line1, LV_ALIGN_CENTER, 0, -20);

    s_label_line2 = lv_label_create(dial);
    lv_obj_set_width(s_label_line2, SAFE_SIZE - 32);
    lv_obj_set_style_text_color(s_label_line2, lv_color_hex(0xaeb6d5), 0);
    lv_obj_set_style_text_font(s_label_line2, font_small(), 0);
    lv_label_set_long_mode(s_label_line2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_label_line2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_label_line2, s_label_line1, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    // Progress bar (for track position) - horizontal, higher to avoid circle clip
    s_progress_bar = lv_bar_create(dial);
    lv_obj_set_size(s_progress_bar, 140, 3);  // Shorter to fit in circle
    lv_obj_align(s_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -30);  // Higher up
    lv_bar_set_range(s_progress_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x333333), 0);  // Dark gray background
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x8a6fb0), LV_PART_INDICATOR); // Purple
    lv_obj_set_style_pad_all(s_progress_bar, 0, 0);
    lv_obj_set_style_radius(s_progress_bar, 2, 0);

    // Volume bar - vertical on right side, lower
    s_volume_bar = lv_bar_create(dial);
    lv_obj_set_size(s_volume_bar, 5, 60);  // Thinner and shorter
    lv_obj_align(s_volume_bar, LV_ALIGN_RIGHT_MID, -18, 25);  // Right side, lower position
    lv_bar_set_range(s_volume_bar, 0, 100);
    lv_bar_set_mode(s_volume_bar, LV_BAR_MODE_RANGE);  // For vertical fill from bottom
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_hex(0x333333), 0);  // Dark gray background
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_hex(0x5a8fc7), LV_PART_INDICATOR); // Blue
    lv_obj_set_style_pad_all(s_volume_bar, 0, 0);
    lv_obj_set_style_radius(s_volume_bar, 2, 0);

    // Volume icon (speaker) - below and left of volume bar
    lv_obj_t *vol_icon = lv_label_create(dial);
    lv_obj_remove_style_all(vol_icon);
    lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(vol_icon, lv_color_hex(0xD0D0D0), 0);  // Light gray for visibility
    lv_obj_set_style_text_font(vol_icon, font_normal(), 0);
    lv_obj_align(vol_icon, LV_ALIGN_RIGHT_MID, -38, 50);  // Below and left of bar

    // Play/pause indicator - above progress bar
    s_paused_label = lv_label_create(dial);
    lv_obj_remove_style_all(s_paused_label);
    lv_label_set_text(s_paused_label, LV_SYMBOL_PLAY);  // Show play icon by default
    lv_obj_set_style_text_color(s_paused_label, lv_color_hex(0xFFFFFF), 0);  // Bright white for visibility
    lv_obj_set_style_text_font(s_paused_label, font_large(), 0);  // Larger for icon
    lv_obj_align(s_paused_label, LV_ALIGN_BOTTOM_MID, 0, -50);  // Above progress bar

    apply_state(&s_pending);
}

static void apply_state(const struct ui_state *state) {
    lv_label_set_text(s_label_line1, state->line1);
    lv_label_set_text(s_label_line2, state->line2);
    lv_bar_set_value(s_volume_bar, state->volume, LV_ANIM_OFF);

    // Update progress bar
    if (state->length > 0) {
        int progress = (int)((state->seek_position * 1000) / state->length);
        if (progress > 1000) progress = 1000;
        lv_bar_set_value(s_progress_bar, progress, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    }

    // Update play/pause indicator icon
    lv_label_set_text(s_paused_label, state->playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    set_status_dot(state->online);
}

static void set_status_dot(bool online) {
    lv_color_t color = online ? lv_color_hex(0x41db64) : lv_color_hex(0xdb4154);
    lv_obj_set_style_bg_color(s_status_dot, color, 0);
}

void ui_set_input_handler(ui_input_cb_t handler) {
    s_input_cb = handler;
}

void ui_dispatch_input(ui_input_event_t ev) {
    if (s_input_cb) {
        s_input_cb(ev);
    }
}

void ui_set_zone_name(const char *zone_name) {
    if (!zone_name || !s_zone_label) return;
    lv_label_set_text(s_zone_label, zone_name);
    snprintf(s_zone_name, sizeof(s_zone_name), "%s", zone_name);
}

void ui_set_message(const char *msg) {
    if (!msg) return;
    os_mutex_lock(&s_state_lock);
    snprintf(s_pending_message, sizeof(s_pending_message), "%s", msg);
    s_message_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_loop_iter(void) {
    lv_tick_inc(10);  // Match the 10ms delay in ui_loop_task
    platform_task_run_pending();
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

    // Create zone list container - circular to match display
    lv_obj_t *list_bg = lv_obj_create(s_zone_picker_container);
    lv_obj_remove_style_all(list_bg);
    lv_obj_set_size(list_bg, SAFE_SIZE, SAFE_SIZE);
    lv_obj_set_style_bg_color(list_bg, lv_color_hex(0x000000), 0);  // Pure black for consistency
    lv_obj_set_style_bg_opa(list_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list_bg, SAFE_SIZE / 2, 0);  // Circular
    lv_obj_set_style_border_width(list_bg, 2, 0);
    lv_obj_set_style_border_color(list_bg, lv_color_hex(0x3a3c44), 0);
    lv_obj_center(list_bg);

    // Title
    lv_obj_t *title = lv_label_create(list_bg);
    lv_label_set_text(title, "Select Zone");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, font_normal(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 25);  // Lower to avoid circular clip

    // Zone list - narrower and shorter to fit in circle
    s_zone_list = lv_list_create(list_bg);
    lv_obj_set_size(s_zone_list, 160, 120);  // Narrower to fit in circular boundary
    lv_obj_align(s_zone_list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_zone_list, lv_color_hex(0x000000), 0);  // Pure black
    lv_obj_set_style_border_width(s_zone_list, 0, 0);

    for (int i = 0; i < zone_count; i++) {
        lv_obj_t *btn = lv_list_add_button(s_zone_list, NULL, zone_names[i]);
        lv_obj_set_style_bg_color(btn, lv_color_hex(i == selected_idx ? 0x2a5a9a : 0x000000), 0);  // Pure black for unselected
        lv_obj_set_style_text_color(btn, lv_color_hex(0xffffff), 0);

        // Add click handler to select zone and store zone index in user data
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, zone_button_event_cb, LV_EVENT_CLICKED, NULL);
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
            lv_obj_set_style_bg_color(old_btn, lv_color_hex(0x000000), 0);  // Pure black for unselected
        }

        // Update new button
        s_zone_picker_selected = new_selected;
        lv_obj_t *new_btn = lv_obj_get_child(s_zone_list, s_zone_picker_selected);
        if (new_btn) {
            lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x2a5a9a), 0);  // Blue for selected
            lv_obj_scroll_to_view(new_btn, LV_ANIM_ON);
        }
    }
}

static void zone_label_event_cb(lv_event_t *e) {
    (void)e;
    ui_dispatch_input(UI_INPUT_MENU);
}

static void zone_button_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn) return;

    // Update selected index from button's user data
    int zone_idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    s_zone_picker_selected = zone_idx;

    // Update button colors to reflect new selection
    uint32_t child_count = lv_obj_get_child_count(s_zone_list);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(s_zone_list, i);
        if (child) {
            lv_obj_set_style_bg_color(child, lv_color_hex(i == (uint32_t)zone_idx ? 0x2a5a9a : 0x000000), 0);
        }
    }

    // Dispatch play/pause to trigger zone selection
    ui_dispatch_input(UI_INPUT_PLAY_PAUSE);
}

static void show_message_overlay(const char *msg) {
    // Clean up existing overlay
    if (s_message_overlay) {
        lv_obj_del(s_message_overlay);
        s_message_overlay = NULL;
        s_message_label = NULL;
    }
    if (s_message_timer) {
        lv_timer_del(s_message_timer);
        s_message_timer = NULL;
    }

    // Create overlay
    s_message_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_message_overlay);
    lv_obj_set_size(s_message_overlay, 180, 60);
    lv_obj_set_style_bg_color(s_message_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_message_overlay, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_message_overlay, 12, 0);
    lv_obj_center(s_message_overlay);

    s_message_label = lv_label_create(s_message_overlay);
    lv_obj_set_style_text_font(s_message_label, font_normal(), 0);
    lv_obj_set_style_text_color(s_message_label, lv_color_hex(0xffffff), 0);
    lv_label_set_text(s_message_label, msg);
    lv_obj_center(s_message_label);

    // Auto-hide after 2 seconds
    s_message_timer = lv_timer_create(hide_message_overlay, 2000, NULL);
    lv_timer_set_repeat_count(s_message_timer, 1);
}

static void hide_message_overlay(lv_timer_t *timer) {
    (void)timer;
    if (s_message_overlay) {
        lv_obj_del(s_message_overlay);
        s_message_overlay = NULL;
        s_message_label = NULL;
    }
    s_message_timer = NULL;
}
