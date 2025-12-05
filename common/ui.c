// Roon Knob UI - Clean design based on smart-knob approach
// Uses LVGL default theme + minimal manual styling

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "os_mutex.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"
#include "platform/platform_http.h"
#include "lvgl.h"
#include "ui.h"
#include "roon_client.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "battery.h"
#include "ui_jpeg.h"  // JPEG decoder helper
#define UI_TAG "ui"
#else
#define UI_TAG "ui"
#define ESP_LOGI(tag, fmt, ...) printf("[I] " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W] " tag ": " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E] " tag ": " fmt "\n", ##__VA_ARGS__)
#endif

#ifdef ESP_PLATFORM
#define SCREEN_SIZE 360
#else
#define SCREEN_SIZE 240
#endif

// Smart-knob inspired color palette - using hex for cleaner code
#define COLOR_WHITE         lv_color_hex(0xffffff)
#define COLOR_GREY          lv_color_hex(0x5a5a5a)
#define COLOR_DARK_GREY     lv_color_hex(0x3c3c3c)

struct ui_state {
    char line1[128];
    char line2[128];
    char zone_name[64];
    bool playing;
    int volume;
    int volume_min;
    int volume_max;
    bool online;
    int seek_position;
    int length;
};

// UI widgets - Blue Knob inspired design
static lv_obj_t *s_track_label;        // Main track name
static lv_obj_t *s_artist_label;       // Artist/album
static lv_obj_t *s_volume_arc;         // Outer arc for volume
static lv_obj_t *s_progress_arc;       // Inner arc for track progress
static lv_obj_t *s_volume_label;       // Volume percentage (small, top)
static lv_obj_t *s_volume_overlay;     // Large volume popup when adjusting
static lv_obj_t *s_volume_overlay_label;  // Volume text in overlay
static lv_timer_t *s_volume_overlay_timer;  // Timer to hide volume overlay
static lv_obj_t *s_status_dot;         // Online/offline indicator
static lv_obj_t *s_battery_label;      // Battery status
static lv_obj_t *s_zone_label;         // Zone name
static lv_obj_t *s_btn_prev;           // Previous track button
static lv_obj_t *s_btn_play;           // Play/pause button (center, large)
static lv_obj_t *s_btn_next;           // Next track button
static lv_obj_t *s_play_icon;          // Play/pause icon label
static lv_obj_t *s_background;         // Light background container

// Artwork layers
static lv_obj_t *s_artwork_container;  // Container for artwork layers
static lv_obj_t *s_artwork_image;      // Album art image
static lv_obj_t *s_ui_container;       // Container for all UI widgets

// Reusable styles - smart-knob inspired
static lv_style_t style_button_primary;    // Center play/pause button
static lv_style_t style_button_secondary;  // Prev/next buttons
static lv_style_t style_button_label;      // Button text labels

// Status bar at bottom
static lv_obj_t *s_status_bar;         // Status bar label at bottom
static lv_timer_t *s_status_timer;     // Timer to clear status messages

// Zone picker - using LVGL roller widget
static lv_obj_t *s_zone_picker_overlay;    // Dark background overlay
static lv_obj_t *s_zone_roller;            // Roller widget for zone selection
static bool s_zone_picker_visible = false;
#define MAX_ZONE_PICKER_ZONES 16
#define MAX_ZONE_ID_LEN 48
static char s_zone_picker_ids[MAX_ZONE_PICKER_ZONES][MAX_ZONE_ID_LEN];  // Store zone IDs
static int s_zone_picker_count = 0;

// OTA update notification
static lv_obj_t *s_update_btn;             // Update notification button
static char s_update_version[32] = "";     // Available update version
static int s_update_progress = -1;         // Update download progress (-1 = not updating)

// State management
static os_mutex_t s_state_lock = OS_MUTEX_INITIALIZER;
static struct ui_state s_pending = {
    .line1 = "Starting...",
    .line2 = "",
    .zone_name = "",
    .playing = false,
    .volume = 0,
    .volume_min = -80,
    .volume_max = 0,
    .online = false,
    .seek_position = 0,
    .length = 0,
};
static bool s_dirty = true;
static char s_pending_message[128] = "";
static bool s_message_dirty = false;
static bool s_zone_name_dirty = false;
static ui_input_cb_t s_input_cb;
static char s_last_image_key[128] = "";  // Track last loaded artwork
#ifdef ESP_PLATFORM
static ui_jpeg_image_t s_artwork_img;  // Decoded RGB565 image for artwork (ESP32)
#else
static char *s_artwork_data = NULL;  // Raw JPEG data for PC simulator
#endif

// Fonts - larger sizes for better readability
static inline const lv_font_t *font_small(void) { return &lv_font_montserrat_20; }
static inline const lv_font_t *font_normal(void) { return &lv_font_montserrat_28; }
static inline const lv_font_t *font_large(void) { return &lv_font_montserrat_48; }

// Forward declarations
static void apply_state(const struct ui_state *state);
static void build_layout(void);
static void poll_pending(lv_timer_t *timer);
static void set_status_dot(bool online);
static void zone_label_event_cb(lv_event_t *e);
static void zone_label_long_press_cb(lv_event_t *e);
static void btn_prev_event_cb(lv_event_t *e);
static void btn_play_event_cb(lv_event_t *e);
static void btn_next_event_cb(lv_event_t *e);
static void zone_roller_event_cb(lv_event_t *e);
static void show_status_message(const char *message);
static void clear_status_message_timer_cb(lv_timer_t *timer);
static void update_battery_display(void);
static void show_volume_overlay(int volume);

// ============================================================================
// UI Initialization
// ============================================================================

void ui_init(void) {
    // Don't use theme - it causes ugly color overrides
    // We'll style everything manually for full control

    ESP_LOGI(UI_TAG, "Using ESP_NEW_JPEG software decoder for artwork");

    build_layout();

    // Poll for state updates every 50ms
    lv_timer_t *poll_timer = lv_timer_create(poll_pending, 50, NULL);
    if (poll_timer) {
        lv_timer_set_repeat_count(poll_timer, -1);
    } else {
        ESP_LOGE(UI_TAG, "FAILED to create poll_pending timer!");
    }
}

// ============================================================================
// Styles - Smart-knob inspired reusable styles
// ============================================================================

static void create_styles(void) {
    // Primary button style (center play/pause) - override ALL theme colors
    lv_style_init(&style_button_primary);
    lv_style_set_radius(&style_button_primary, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&style_button_primary, lv_color_hex(0x2c2c2c));  // Dark grey
    lv_style_set_bg_opa(&style_button_primary, LV_OPA_COVER);
    lv_style_set_border_width(&style_button_primary, 3);
    lv_style_set_border_color(&style_button_primary, lv_color_hex(0x5a9fd4));  // Light blue
    lv_style_set_border_opa(&style_button_primary, LV_OPA_COVER);
    lv_style_set_shadow_width(&style_button_primary, 0);

    // Secondary button style (prev/next) - override ALL theme colors
    lv_style_init(&style_button_secondary);
    lv_style_set_radius(&style_button_secondary, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&style_button_secondary, lv_color_hex(0x1a1a1a));  // Darker grey
    lv_style_set_bg_opa(&style_button_secondary, LV_OPA_COVER);
    lv_style_set_border_width(&style_button_secondary, 2);
    lv_style_set_border_color(&style_button_secondary, COLOR_GREY);
    lv_style_set_border_opa(&style_button_secondary, LV_OPA_COVER);
    lv_style_set_shadow_width(&style_button_secondary, 0);

    // Button label style
    lv_style_init(&style_button_label);
    lv_style_set_text_color(&style_button_label, lv_color_hex(0xfafafa));  // Off-white
}

// ============================================================================
// Layout - Blue Knob inspired design
// ============================================================================

static void build_layout(void) {
    // Initialize reusable styles first
    create_styles();

    lv_obj_t *screen = lv_screen_active();
    if (!screen) {
        ESP_LOGE(UI_TAG, "lv_screen_active returned NULL!");
        return;
    }

    // Set screen background to pure black
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // Create artwork layers - SIMPLIFIED to avoid memory exhaustion
    // No circular clipping (display is already circular)
    // No semi-transparent overlay (would require layer buffering)

    s_artwork_container = lv_obj_create(screen);
    lv_obj_set_size(s_artwork_container, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_center(s_artwork_container);
    lv_obj_set_style_bg_opa(s_artwork_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_artwork_container, 0, 0);
    lv_obj_set_style_pad_all(s_artwork_container, 0, 0);

    // Create artwork image (hidden initially, shown when loaded)
    s_artwork_image = lv_img_create(s_artwork_container);
    lv_obj_set_size(s_artwork_image, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_center(s_artwork_image);
    lv_obj_add_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);  // Hidden until artwork loads
    // Dim the artwork for better text contrast (avoid overlay layer)
    lv_obj_set_style_img_opa(s_artwork_image, LV_OPA_40, 0);  // 40% opacity = 60% dimming

    // Create UI container directly (no intermediate overlay layer)
    s_ui_container = lv_obj_create(s_artwork_container);
    lv_obj_set_size(s_ui_container, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_center(s_ui_container);
    lv_obj_set_style_bg_opa(s_ui_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui_container, 0, 0);
    lv_obj_set_style_pad_all(s_ui_container, 0, 0);

    // Update background pointer to ui_container for widget creation
    s_background = s_ui_container;

    // Outer volume arc - full circle ring around the display edge
    s_volume_arc = lv_arc_create(s_ui_container);
    lv_obj_set_size(s_volume_arc, SCREEN_SIZE - 10, SCREEN_SIZE - 10);
    lv_obj_center(s_volume_arc);
    lv_arc_set_range(s_volume_arc, 0, 100);
    lv_arc_set_value(s_volume_arc, 0);
    lv_arc_set_bg_angles(s_volume_arc, 0, 359);  // Nearly full circle (360 causes rendering issues)
    lv_arc_set_rotation(s_volume_arc, 270);  // Start at top (12 o'clock)
    lv_arc_set_mode(s_volume_arc, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_width(s_volume_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_volume_arc, 8, LV_PART_INDICATOR);
    lv_obj_remove_flag(s_volume_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_volume_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_volume_arc, 0, LV_PART_KNOB);

    // Arc colors - dark grey background track, blue indicator
    lv_obj_set_style_arc_color(s_volume_arc, lv_color_hex(0x3a3a3a), LV_PART_MAIN);  // Lighter grey for visibility
    lv_obj_set_style_arc_color(s_volume_arc, lv_color_hex(0x5a9fd4), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_volume_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_volume_arc, LV_OPA_COVER, LV_PART_INDICATOR);

    // Inner progress arc - full circle for track playback progress
    s_progress_arc = lv_arc_create(s_ui_container);
    lv_obj_set_size(s_progress_arc, SCREEN_SIZE - 30, SCREEN_SIZE - 30);
    lv_obj_center(s_progress_arc);
    lv_arc_set_range(s_progress_arc, 0, 100);
    lv_arc_set_value(s_progress_arc, 0);
    lv_arc_set_bg_angles(s_progress_arc, 0, 359);  // Nearly full circle
    lv_arc_set_rotation(s_progress_arc, 270);  // Start at top (12 o'clock)
    lv_arc_set_mode(s_progress_arc, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_width(s_progress_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_progress_arc, 4, LV_PART_INDICATOR);
    lv_obj_remove_flag(s_progress_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_progress_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_progress_arc, 0, LV_PART_KNOB);

    // Progress arc colors - subtle grey track, lighter blue indicator
    lv_obj_set_style_arc_color(s_progress_arc, lv_color_hex(0x2a2a2a), LV_PART_MAIN);  // Slightly lighter
    lv_obj_set_style_arc_color(s_progress_arc, lv_color_hex(0x7bb9e8), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_progress_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_progress_arc, LV_OPA_COVER, LV_PART_INDICATOR);

    // Volume overlay - large centered popup that appears when adjusting volume
    s_volume_overlay = lv_obj_create(s_ui_container);
    lv_obj_set_size(s_volume_overlay, 160, 160);
    lv_obj_center(s_volume_overlay);
    lv_obj_set_style_bg_color(s_volume_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_volume_overlay, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_volume_overlay, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_volume_overlay, 2, 0);
    lv_obj_set_style_border_color(s_volume_overlay, lv_color_hex(0x5a9fd4), 0);
    lv_obj_add_flag(s_volume_overlay, LV_OBJ_FLAG_HIDDEN);

    s_volume_overlay_label = lv_label_create(s_volume_overlay);
    lv_obj_set_style_text_font(s_volume_overlay_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_volume_overlay_label, lv_color_hex(0xfafafa), 0);
    lv_label_set_text(s_volume_overlay_label, "0 dB");
    lv_obj_center(s_volume_overlay_label);

    // Volume label - small text at top
    s_volume_label = lv_label_create(s_ui_container);
    lv_label_set_text(s_volume_label, "-- dB");
    lv_obj_set_style_text_font(s_volume_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_volume_label, lv_color_hex(0xfafafa), 0);  // Off-white
    lv_obj_align(s_volume_label, LV_ALIGN_TOP_MID, 0, 12);

    // Status dot - top right (on the outer ring)
    s_status_dot = lv_obj_create(s_ui_container);
    lv_obj_set_size(s_status_dot, 10, 10);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_align(s_volume_label, LV_ALIGN_OUT_RIGHT_MID, 15, 0);

    // Battery indicator - top left
    s_battery_label = lv_label_create(s_ui_container);
    lv_label_set_text(s_battery_label, "");
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_LEFT, 15, 12);

    // Zone label - clickable zone name, positioned below the arc edge
    s_zone_label = lv_label_create(s_ui_container);
    lv_label_set_text(s_zone_label, s_pending.zone_name);
    lv_obj_set_style_text_font(s_zone_label, font_normal(), 0);  // Normal font for readability
    lv_obj_set_style_text_color(s_zone_label, lv_color_hex(0xbbbbbb), 0);  // Light grey, visible
    lv_obj_set_width(s_zone_label, SCREEN_SIZE - 120);  // Limit width for circular display
    lv_obj_set_style_text_align(s_zone_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_zone_label, LV_LABEL_LONG_DOT);  // Truncate with ... if too long
    lv_obj_align(s_zone_label, LV_ALIGN_TOP_MID, 0, 50);  // Move down to avoid arc edge
    lv_obj_add_flag(s_zone_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_zone_label, zone_label_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_zone_label, zone_label_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    // Add press state visual feedback
    lv_obj_set_style_text_color(s_zone_label, lv_color_hex(0xfafafa), LV_STATE_PRESSED);

    // Track name - positioned just above media controls (buttons are at CENTER + 60)
    // Place track at CENTER - 20 to sit nicely above the buttons
    s_track_label = lv_label_create(s_background);
    lv_obj_set_width(s_track_label, SCREEN_SIZE - 80);
    lv_obj_set_height(s_track_label, LV_SIZE_CONTENT);  // Limit to single line
    lv_obj_set_style_text_font(s_track_label, font_normal(), 0);
    lv_obj_set_style_text_align(s_track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_track_label, lv_color_hex(0xfafafa), 0);  // Off-white for primary text
    lv_label_set_long_mode(s_track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);  // Slow scroll for long text
    lv_obj_set_style_anim_duration(s_track_label, 8000, LV_PART_MAIN);  // 8 second scroll cycle
    lv_obj_set_style_max_height(s_track_label, 30, 0);  // Limit height to prevent overflow
    lv_obj_align(s_track_label, LV_ALIGN_CENTER, 0, -20);  // Just above media controls
    lv_label_set_text(s_track_label, s_pending.line1);

    // Artist - positioned above track name with smaller font
    s_artist_label = lv_label_create(s_background);
    lv_obj_set_width(s_artist_label, SCREEN_SIZE - 80);
    lv_obj_set_height(s_artist_label, LV_SIZE_CONTENT);  // Limit to single line
    lv_obj_set_style_text_font(s_artist_label, font_small(), 0);
    lv_obj_set_style_text_align(s_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_artist_label, COLOR_GREY, 0);  // Grey for secondary text
    lv_label_set_long_mode(s_artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);  // Slow scroll for long text
    lv_obj_set_style_anim_duration(s_artist_label, 8000, LV_PART_MAIN);  // 8 second scroll cycle
    lv_obj_set_style_max_height(s_artist_label, 25, 0);  // Limit height to prevent overflow
    lv_obj_align(s_artist_label, LV_ALIGN_CENTER, 0, -55);  // Above track name
    lv_label_set_text(s_artist_label, s_pending.line2);

    // Media control buttons - Blue Knob style (3 circular buttons)
    int btn_y = 60;  // Offset from center - move lower
    int btn_spacing = 70;  // Space between buttons

    // Previous button (left) - use lv_btn_create for proper button widget
    s_btn_prev = lv_btn_create(s_background);
    lv_obj_set_size(s_btn_prev, 50, 50);
    lv_obj_add_style(s_btn_prev, &style_button_secondary, 0);
    lv_obj_align(s_btn_prev, LV_ALIGN_CENTER, -btn_spacing, btn_y);
    lv_obj_add_event_cb(s_btn_prev, btn_prev_event_cb, LV_EVENT_CLICKED, NULL);

    // Override ALL states to prevent theme colors
    lv_obj_set_style_bg_color(s_btn_prev, lv_color_hex(0x1a1a1a), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_prev, lv_color_hex(0x3c3c3c), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_btn_prev, COLOR_GREY, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_btn_prev, lv_color_hex(0x5a9fd4), LV_STATE_PRESSED);

    lv_obj_t *prev_label = lv_label_create(s_btn_prev);
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(prev_label, font_normal(), 0);
    lv_obj_add_style(prev_label, &style_button_label, 0);
    lv_obj_center(prev_label);

    // Play/Pause button (center, larger)
    s_btn_play = lv_btn_create(s_background);
    lv_obj_set_size(s_btn_play, 70, 70);
    lv_obj_add_style(s_btn_play, &style_button_primary, 0);
    lv_obj_align(s_btn_play, LV_ALIGN_CENTER, 0, btn_y);
    lv_obj_add_event_cb(s_btn_play, btn_play_event_cb, LV_EVENT_CLICKED, NULL);

    // Override ALL states to prevent theme colors
    lv_obj_set_style_bg_color(s_btn_play, lv_color_hex(0x2c2c2c), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_play, lv_color_hex(0x3c3c3c), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_btn_play, lv_color_hex(0x5a9fd4), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_btn_play, lv_color_hex(0x7bb9e8), LV_STATE_PRESSED);

    s_play_icon = lv_label_create(s_btn_play);
    lv_label_set_text(s_play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(s_play_icon, font_large(), 0);
    lv_obj_add_style(s_play_icon, &style_button_label, 0);
    lv_obj_center(s_play_icon);

    // Next button (right)
    s_btn_next = lv_btn_create(s_background);
    lv_obj_set_size(s_btn_next, 50, 50);
    lv_obj_add_style(s_btn_next, &style_button_secondary, 0);
    lv_obj_align(s_btn_next, LV_ALIGN_CENTER, btn_spacing, btn_y);
    lv_obj_add_event_cb(s_btn_next, btn_next_event_cb, LV_EVENT_CLICKED, NULL);

    // Override ALL states to prevent theme colors
    lv_obj_set_style_bg_color(s_btn_next, lv_color_hex(0x1a1a1a), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_next, lv_color_hex(0x3c3c3c), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_btn_next, COLOR_GREY, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_btn_next, lv_color_hex(0x5a9fd4), LV_STATE_PRESSED);

    lv_obj_t *next_label = lv_label_create(s_btn_next);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(next_label, font_normal(), 0);
    lv_obj_add_style(next_label, &style_button_label, 0);
    lv_obj_center(next_label);

    // Status bar at bottom - small text for messages like "Bridge is ready"
    s_status_bar = lv_label_create(s_ui_container);
    lv_label_set_text(s_status_bar, "");
    lv_obj_set_width(s_status_bar, SCREEN_SIZE - 40);
    lv_obj_set_style_text_font(s_status_bar, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_status_bar, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status_bar, LV_LABEL_LONG_DOT);
    lv_obj_align(s_status_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ============================================================================
// Event Handlers
// ============================================================================

static void zone_label_event_cb(lv_event_t *e) {
    (void)e;
    if (s_input_cb) {
        s_input_cb(UI_INPUT_MENU);
    }
}

static void zone_label_long_press_cb(lv_event_t *e) {
    (void)e;
    ui_show_settings();
}

static void btn_prev_event_cb(lv_event_t *e) {
    (void)e;
    if (s_input_cb) {
        s_input_cb(UI_INPUT_PREV_TRACK);
    }
}

static void btn_play_event_cb(lv_event_t *e) {
    (void)e;
    if (s_input_cb) {
        s_input_cb(UI_INPUT_PLAY_PAUSE);
    }
}

static void btn_next_event_cb(lv_event_t *e) {
    (void)e;
    if (s_input_cb) {
        s_input_cb(UI_INPUT_NEXT_TRACK);
    }
}

static void zone_roller_event_cb(lv_event_t *e) {
    (void)e;
    if (s_input_cb) {
        s_input_cb(UI_INPUT_PLAY_PAUSE);  // Trigger zone selection
    }
}

// ============================================================================
// State Management
// ============================================================================

static void apply_state(const struct ui_state *state) {
    // Update track/artist labels
    if (s_track_label && s_artist_label) {
        lv_label_set_text(s_track_label, state->line1);
        lv_obj_invalidate(s_track_label);

        lv_label_set_text(s_artist_label, state->line2);
        lv_obj_invalidate(s_artist_label);
    } else {
        ESP_LOGE(UI_TAG, "Label pointers are NULL! track=%p artist=%p", s_track_label, s_artist_label);
    }

    // Update volume arc and label, show overlay if volume changed
    // Volume is in dB with zone-specific min/max range
    static int last_volume = -9999;  // Sentinel value (unlikely real volume)
    static bool volume_initialized = false;
    if (volume_initialized && last_volume != state->volume) {
        show_volume_overlay(state->volume);
    }
    volume_initialized = true;
    last_volume = state->volume;

    // Convert dB to 0-100 scale for arc display using zone's actual min/max
    int vol_range = state->volume_max - state->volume_min;
    int vol_pct = 0;
    if (vol_range > 0) {
        vol_pct = ((state->volume - state->volume_min) * 100) / vol_range;
    }
    if (vol_pct < 0) vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;
    lv_arc_set_value(s_volume_arc, vol_pct);

    // Display volume in dB
    char vol_text[16];
    snprintf(vol_text, sizeof(vol_text), "%d dB", state->volume);
    lv_label_set_text(s_volume_label, vol_text);

    // Update progress arc based on seek position and track length
    if (s_progress_arc && state->length > 0) {
        int progress_pct = (state->seek_position * 100) / state->length;
        if (progress_pct > 100) progress_pct = 100;
        if (progress_pct < 0) progress_pct = 0;
        lv_arc_set_value(s_progress_arc, progress_pct);
    } else if (s_progress_arc) {
        lv_arc_set_value(s_progress_arc, 0);
    }

    // Update play/pause icon
    if (s_play_icon) {
        if (state->playing) {
            lv_label_set_text(s_play_icon, LV_SYMBOL_PAUSE);
        } else {
            lv_label_set_text(s_play_icon, LV_SYMBOL_PLAY);
        }
    }

    // Update online status
    set_status_dot(state->online);

    // Update battery
    update_battery_display();
}

static void set_status_dot(bool online) {
    if (online) {
        lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0x00ff00), 0);  // Green
    } else {
        lv_obj_set_style_bg_color(s_status_dot, COLOR_GREY, 0);
    }
}

static void poll_pending(lv_timer_t *timer) {
    (void)timer;

    os_mutex_lock(&s_state_lock);
    bool dirty = s_dirty;
    struct ui_state local_state = s_pending;
    s_dirty = false;

    bool show_message = s_message_dirty;
    char message[128];
    if (show_message) {
        strncpy(message, s_pending_message, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        s_message_dirty = false;
    }

    bool zone_name_changed = s_zone_name_dirty;
    char zone_name[64];
    if (zone_name_changed) {
        strncpy(zone_name, s_pending.zone_name, sizeof(zone_name) - 1);
        zone_name[sizeof(zone_name) - 1] = '\0';
        s_zone_name_dirty = false;
    }
    os_mutex_unlock(&s_state_lock);

    if (dirty) {
        apply_state(&local_state);
    }
    if (show_message) {
        show_status_message(message);
    }
    if (zone_name_changed && s_zone_label) {
        lv_label_set_text(s_zone_label, zone_name);
    }
}

static void update_battery_display(void) {
#ifdef ESP_PLATFORM
    int percent = battery_get_percentage();
    bool charging = battery_is_charging();

    if (percent < 0) {
        lv_label_set_text(s_battery_label, "");
        return;
    }

    char buf[16];
    if (charging) {
        snprintf(buf, sizeof(buf), "\xE2\x9A\xA1 %d%%", percent);
    } else {
        snprintf(buf, sizeof(buf), "%d%%", percent);
    }
    lv_label_set_text(s_battery_label, buf);

    // Warning color for low battery
    if (percent < 20 && !charging) {
        lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0xff0000), 0);  // Red
    } else {
        lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0xfafafa), 0);  // Off-white
    }
#endif
}

// ============================================================================
// Status Bar
// ============================================================================

static void show_status_message(const char *message) {
    if (!s_status_bar) {
        ESP_LOGW(UI_TAG, "Status bar not initialized!");
        return;
    }

    lv_label_set_text(s_status_bar, message);

    // Auto-clear after 3 seconds
    if (s_status_timer) {
        lv_timer_reset(s_status_timer);
    } else {
        s_status_timer = lv_timer_create(clear_status_message_timer_cb, 3000, NULL);
        lv_timer_set_repeat_count(s_status_timer, 1);
    }
}

static void clear_status_message_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_status_bar) {
        lv_label_set_text(s_status_bar, "");
    }
    s_status_timer = NULL;
}

// ============================================================================
// Volume Overlay - Shows large volume indicator when adjusting
// ============================================================================

static void hide_volume_overlay_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_volume_overlay) {
        lv_obj_add_flag(s_volume_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    s_volume_overlay_timer = NULL;
}

static void show_volume_overlay(int volume) {
    if (!s_volume_overlay || !s_volume_overlay_label) {
        return;
    }

    // Update the volume text (display in dB)
    char vol_text[16];
    snprintf(vol_text, sizeof(vol_text), "%d dB", volume);
    lv_label_set_text(s_volume_overlay_label, vol_text);
    lv_obj_center(s_volume_overlay_label);

    // Show the overlay
    lv_obj_clear_flag(s_volume_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_volume_overlay);

    // Reset/create the hide timer (1.5 seconds)
    if (s_volume_overlay_timer) {
        lv_timer_reset(s_volume_overlay_timer);
    } else {
        s_volume_overlay_timer = lv_timer_create(hide_volume_overlay_timer_cb, 1500, NULL);
        lv_timer_set_repeat_count(s_volume_overlay_timer, 1);
    }
}

// ============================================================================
// Zone Picker - LVGL Roller Widget
// ============================================================================

void ui_show_zone_picker(const char **zone_names, const char **zone_ids, int count, int selected) {
    if (s_zone_picker_visible) {
        return;
    }

    // Store zone IDs for later retrieval
    s_zone_picker_count = (count > MAX_ZONE_PICKER_ZONES) ? MAX_ZONE_PICKER_ZONES : count;
    for (int i = 0; i < s_zone_picker_count; i++) {
        strncpy(s_zone_picker_ids[i], zone_ids[i], MAX_ZONE_ID_LEN - 1);
        s_zone_picker_ids[i][MAX_ZONE_ID_LEN - 1] = '\0';
    }

    // Create fullscreen dark overlay
    s_zone_picker_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_zone_picker_overlay, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_center(s_zone_picker_overlay);
    lv_obj_set_style_bg_color(s_zone_picker_overlay, lv_color_hex(0x000000), 0);  // Pure black
    lv_obj_set_style_bg_opa(s_zone_picker_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_zone_picker_overlay, 0, 0);
    lv_obj_set_style_radius(s_zone_picker_overlay, 0, 0);

    // Title at top
    lv_obj_t *title = lv_label_create(s_zone_picker_overlay);
    lv_label_set_text(title, "SELECT ZONE");
    lv_obj_set_style_text_font(title, font_normal(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Create roller widget - native LVGL scrollable picker
    s_zone_roller = lv_roller_create(s_zone_picker_overlay);
    lv_obj_set_width(s_zone_roller, SCREEN_SIZE - 80);

    // Build options string (newline-separated)
    char options[1024] = "";
    for (int i = 0; i < s_zone_picker_count; i++) {
        if (i > 0) strcat(options, "\n");
        strncat(options, zone_names[i], sizeof(options) - strlen(options) - 1);
    }
    lv_roller_set_options(s_zone_roller, options, LV_ROLLER_MODE_INFINITE);

    // Set selected zone
    lv_roller_set_selected(s_zone_roller, selected, LV_ANIM_OFF);

    // Show 5 rows at once
    lv_roller_set_visible_row_count(s_zone_roller, 5);

    // Add click event handler to select zone
    lv_obj_add_event_cb(s_zone_roller, zone_roller_event_cb, LV_EVENT_CLICKED, NULL);

    // Style the roller - good contrast for readability
    lv_obj_set_style_text_font(s_zone_roller, font_normal(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_zone_roller, font_normal(), LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_zone_roller, lv_color_hex(0xaaaaaa), LV_PART_MAIN);  // Light grey for non-selected

    // Style the roller background
    lv_obj_set_style_bg_color(s_zone_roller, lv_color_hex(0x0a0a0a), LV_PART_MAIN);  // Very dark background
    lv_obj_set_style_bg_opa(s_zone_roller, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_zone_roller, 0, LV_PART_MAIN);

    // Style selected item - bright white text with blue highlight
    lv_obj_set_style_bg_color(s_zone_roller, lv_color_hex(0x5a9fd4), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(s_zone_roller, LV_OPA_50, LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_zone_roller, lv_color_hex(0xffffff), LV_PART_SELECTED);

    lv_obj_center(s_zone_roller);

    // Hint text at bottom
    lv_obj_t *hint = lv_label_create(s_zone_picker_overlay);
    lv_label_set_text(hint, "Turn knob or swipe\nTap to select");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    s_zone_picker_visible = true;
}

void ui_hide_zone_picker(void) {
    if (!s_zone_picker_visible) {
        return;
    }

    if (s_zone_picker_overlay) {
        lv_obj_delete(s_zone_picker_overlay);
        s_zone_picker_overlay = NULL;
        s_zone_roller = NULL;
    }
    s_zone_picker_visible = false;
}

int ui_get_zone_picker_selected(void) {
    if (!s_zone_picker_visible || !s_zone_roller) {
        return -1;  // Invalid state - picker not visible
    }
    return (int)lv_roller_get_selected(s_zone_roller);
}

void ui_zone_picker_get_selected_id(char *out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (!s_zone_picker_visible || !s_zone_roller) {
        return;
    }
    int selected = (int)lv_roller_get_selected(s_zone_roller);
    // Normalize index for infinite mode (modulo actual count)
    if (s_zone_picker_count > 0) {
        selected = selected % s_zone_picker_count;
        strncpy(out, s_zone_picker_ids[selected], len - 1);
        out[len - 1] = '\0';
    }
}

// ============================================================================
// Public API
// ============================================================================

void ui_loop_iter(void) {
    lv_task_handler();
    lv_timer_handler();

    platform_task_run_pending();  // Process callbacks from roon_client thread

    // Check for pending UI updates (poll_pending inline - no timer needed)
    poll_pending(NULL);
}

void ui_set_track(const char *line1, const char *line2) {
    os_mutex_lock(&s_state_lock);
    strncpy(s_pending.line1, line1, sizeof(s_pending.line1) - 1);
    strncpy(s_pending.line2, line2, sizeof(s_pending.line2) - 1);
    s_pending.line1[sizeof(s_pending.line1) - 1] = '\0';
    s_pending.line2[sizeof(s_pending.line2) - 1] = '\0';
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_set_volume(int vol) {
    os_mutex_lock(&s_state_lock);
    s_pending.volume = vol;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_set_volume_with_range(int vol, int vol_min, int vol_max) {
    os_mutex_lock(&s_state_lock);
    s_pending.volume = vol;
    s_pending.volume_min = vol_min;
    s_pending.volume_max = vol_max;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_show_volume_change(int vol) {
    show_volume_overlay(vol);
}

void ui_set_playing(bool playing) {
    os_mutex_lock(&s_state_lock);
    s_pending.playing = playing;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_set_online(bool online) {
    os_mutex_lock(&s_state_lock);
    s_pending.online = online;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_set_zone_name(const char *zone_name) {
    os_mutex_lock(&s_state_lock);
    if (zone_name) {
        strncpy(s_pending.zone_name, zone_name, sizeof(s_pending.zone_name) - 1);
        s_pending.zone_name[sizeof(s_pending.zone_name) - 1] = '\0';
        s_zone_name_dirty = true;
    }
    os_mutex_unlock(&s_state_lock);
}

void ui_set_message(const char *message) {
    os_mutex_lock(&s_state_lock);
    strncpy(s_pending_message, message, sizeof(s_pending_message) - 1);
    s_pending_message[sizeof(s_pending_message) - 1] = '\0';
    s_message_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_set_input_callback(ui_input_cb_t cb) {
    s_input_cb = cb;
}

void ui_dispatch_input(ui_input_event_t input) {
    if (s_input_cb) {
        s_input_cb(input);
    }
}

void ui_set_progress(int seek_ms, int length_ms) {
    os_mutex_lock(&s_state_lock);
    s_pending.seek_position = seek_ms;
    s_pending.length = length_ms;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

// ============================================================================
// Backward Compatibility API Wrappers
// ============================================================================

void ui_set_input_handler(ui_input_cb_t handler) {
    ui_set_input_callback(handler);
}

void ui_update(const char *line1, const char *line2, bool playing, int volume, int volume_min, int volume_max, int seek_position, int length) {
    ui_set_track(line1, line2);
    ui_set_playing(playing);
    ui_set_volume_with_range(volume, volume_min, volume_max);
    ui_set_progress(seek_position, length);
}

void ui_set_status(bool online) {
    ui_set_online(online);
}

// Debug: Test pattern to verify LVGL -> panel color format
void ui_test_pattern(void) {
#ifdef ESP_PLATFORM
    // Log LVGL's actual color values to understand byte order
    lv_color_t red = lv_color_make(0xFF, 0x00, 0x00);
    lv_color_t green = lv_color_make(0x00, 0xFF, 0x00);
    lv_color_t blue = lv_color_make(0x00, 0x00, 0xFF);
    ESP_LOGI(UI_TAG, "LVGL color values:");
    ESP_LOGI(UI_TAG, "  RED   (255,0,0)   = 0x%04X", lv_color_to_u16(red));
    ESP_LOGI(UI_TAG, "  GREEN (0,255,0)   = 0x%04X", lv_color_to_u16(green));
    ESP_LOGI(UI_TAG, "  BLUE  (0,0,255)   = 0x%04X", lv_color_to_u16(blue));

    static uint8_t *test_buf = NULL;
    int w = 360;
    int h = 360;
    size_t sz = w * h * 2;

    if (!test_buf) {
        test_buf = heap_caps_aligned_calloc(16, 1, sz,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!test_buf) {
        ESP_LOGE(UI_TAG, "Failed to allocate test pattern buffer");
        return;
    }

    // Simple solid color blocks - easier to diagnose than gradient
    // Top to bottom: Red, Green, Blue, White
    uint16_t *p = (uint16_t *)test_buf;
    for (int y = 0; y < h; y++) {
        uint16_t c;
        if (y < h / 4) {
            c = lv_color_to_u16(red);  // LVGL Red
        } else if (y < h / 2) {
            c = lv_color_to_u16(green);  // LVGL Green
        } else if (y < 3 * h / 4) {
            c = lv_color_to_u16(blue);  // LVGL Blue
        } else {
            c = 0xFFFF;  // White (same in all formats)
        }
        for (int x = 0; x < w; x++) {
            *p++ = c;
        }
    }

    static lv_image_dsc_t img_dsc;
    memset(&img_dsc, 0, sizeof(img_dsc));
    img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    img_dsc.header.w = w;
    img_dsc.header.h = h;
    img_dsc.data = test_buf;
    img_dsc.data_size = sz;

    lv_image_set_src(s_artwork_image, &img_dsc);
    lv_obj_clear_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_artwork_image, w, h);
    lv_obj_center(s_artwork_image);

    ESP_LOGI(UI_TAG, "Test pattern: 4 solid bars (red/green/blue/white)");
#endif
}

void ui_set_artwork(const char *image_key) {
    // Check if image_key changed
    if (!image_key || !image_key[0]) {
        // No artwork - hide image
        if (s_last_image_key[0]) {
            lv_obj_add_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);
            s_last_image_key[0] = '\0';
        }
        return;
    }

    // Skip if same image
    if (strcmp(image_key, s_last_image_key) == 0) {
        return;
    }

    // Build artwork URL (request 360x360 to match display - no scaling needed)
    // With PSRAM enabled, we can handle the full display resolution
    char url[512];
    if (!roon_client_get_artwork_url(url, sizeof(url), SCREEN_SIZE, SCREEN_SIZE)) {
        ESP_LOGW(UI_TAG, "Failed to build artwork URL");
        return;
    }

    ESP_LOGI(UI_TAG, "Fetching artwork: %s", url);

    // Fetch image data (JPEG)
    char *img_data = NULL;
    size_t img_len = 0;
    int ret = platform_http_get(url, &img_data, &img_len);

    if (ret != 0 || !img_data || img_len == 0) {
        ESP_LOGW(UI_TAG, "Failed to fetch artwork (ret=%d, len=%zu)", ret, img_len);
        platform_http_free(img_data);
        lv_obj_add_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    ESP_LOGI(UI_TAG, "Artwork fetched: %zu bytes", img_len);

#ifdef ESP_PLATFORM
    // Decode JPEG into RGB565 buffer and LVGL descriptor
    ui_jpeg_image_t new_img;
    bool ok = ui_jpeg_decode_to_lvgl((const uint8_t *)img_data,
                                     (int)img_len,
                                     SCREEN_SIZE,
                                     SCREEN_SIZE,
                                     &new_img);

    // HTTP buffer no longer needed
    platform_http_free(img_data);

    if (!ok) {
        ESP_LOGW(UI_TAG, "JPEG decode failed");
        lv_obj_add_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Free previous artwork pixels
    ui_jpeg_free(&s_artwork_img);

    // Take ownership of new pixels and descriptor
    s_artwork_img = new_img;

    // Show it in LVGL
    lv_image_set_src(s_artwork_image, &s_artwork_img.dsc);
    lv_obj_clear_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_artwork_image,
                    s_artwork_img.dsc.header.w,
                    s_artwork_img.dsc.header.h);
    lv_obj_center(s_artwork_image);
    lv_obj_invalidate(s_artwork_image);

    strncpy(s_last_image_key, image_key, sizeof(s_last_image_key) - 1);
    s_last_image_key[sizeof(s_last_image_key) - 1] = '\0';

    ESP_LOGI(UI_TAG, "Artwork displayed");
#else
    // PC simulator - still use raw JPEG (TJPGD or similar)
    if (s_artwork_data) {
        platform_http_free(s_artwork_data);
    }
    s_artwork_data = img_data;

    static lv_image_dsc_t img_dsc;
    img_dsc.header.cf = LV_COLOR_FORMAT_RAW;  // Let LVGL detect format
    img_dsc.header.w = 0;
    img_dsc.header.h = 0;
    img_dsc.data = (const uint8_t *)s_artwork_data;
    img_dsc.data_size = img_len;

    lv_image_set_src(s_artwork_image, &img_dsc);
    lv_obj_clear_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);

    strncpy(s_last_image_key, image_key, sizeof(s_last_image_key) - 1);
    s_last_image_key[sizeof(s_last_image_key) - 1] = '\0';

    ESP_LOGI(UI_TAG, "Artwork displayed (PC sim)");
#endif
}

bool ui_is_zone_picker_visible(void) {
    // Don't log every call - too noisy
    return s_zone_picker_visible;
}

int ui_zone_picker_get_selected(void) {
    return ui_get_zone_picker_selected();
}

void ui_zone_picker_scroll(int delta) {
    if (!s_zone_picker_visible || !s_zone_roller) {
        return;
    }

    // Get current selection and adjust by delta
    uint16_t current = lv_roller_get_selected(s_zone_roller);
    uint16_t option_cnt = lv_roller_get_option_cnt(s_zone_roller);

    // Calculate new position with wraparound
    int new_pos = ((int)current + delta + option_cnt) % option_cnt;

    lv_roller_set_selected(s_zone_roller, new_pos, LV_ANIM_ON);
}

// ============================================================================
// OTA Update UI
// ============================================================================

#ifdef ESP_PLATFORM
#include "ota_update.h"
#endif

static void update_btn_clicked(lv_event_t *e) {
    (void)e;
    ESP_LOGI(UI_TAG, "Update button clicked");
    ui_trigger_update();
}

void ui_set_update_available(const char *version) {
    if (version && version[0]) {
        strncpy(s_update_version, version, sizeof(s_update_version) - 1);
        s_update_version[sizeof(s_update_version) - 1] = '\0';
        ESP_LOGI(UI_TAG, "Update available: %s", s_update_version);

        // Create update button if it doesn't exist
        if (!s_update_btn && s_ui_container) {
            s_update_btn = lv_btn_create(s_ui_container);
            lv_obj_set_size(s_update_btn, 200, 40);
            lv_obj_align(s_update_btn, LV_ALIGN_TOP_MID, 0, 60);
            lv_obj_set_style_bg_color(s_update_btn, lv_color_hex(0x4CAF50), 0);  // Green
            lv_obj_set_style_radius(s_update_btn, 20, 0);

            lv_obj_t *label = lv_label_create(s_update_btn);
            lv_obj_set_style_text_font(label, font_small(), 0);
            lv_obj_center(label);
            lv_obj_set_user_data(s_update_btn, label);  // Store label reference

            lv_obj_add_event_cb(s_update_btn, update_btn_clicked, LV_EVENT_CLICKED, NULL);
        }

        if (s_update_btn) {
            lv_obj_t *label = lv_obj_get_user_data(s_update_btn);
            if (label) {
                char text[64];
                snprintf(text, sizeof(text), LV_SYMBOL_DOWNLOAD " Update to %s", s_update_version);
                lv_label_set_text(label, text);
            }
            lv_obj_clear_flag(s_update_btn, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        s_update_version[0] = '\0';
        if (s_update_btn) {
            lv_obj_add_flag(s_update_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_set_update_progress(int percent) {
    s_update_progress = percent;

    if (s_update_btn) {
        lv_obj_t *label = lv_obj_get_user_data(s_update_btn);
        if (label) {
            if (percent >= 0 && percent <= 100) {
                char text[64];
                snprintf(text, sizeof(text), "Updating... %d%%", percent);
                lv_label_set_text(label, text);
                lv_obj_set_style_bg_color(s_update_btn, lv_color_hex(0x2196F3), 0);  // Blue during update
                lv_obj_clear_flag(s_update_btn, LV_OBJ_FLAG_CLICKABLE);  // Disable clicking during update
            } else if (percent < 0) {
                // Hide or reset
                lv_obj_add_flag(s_update_btn, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(s_update_btn, LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }
}

void ui_trigger_update(void) {
#ifdef ESP_PLATFORM
    ESP_LOGI(UI_TAG, "Triggering OTA update");
    ota_start_update();
#else
    ESP_LOGI(UI_TAG, "OTA update not available on PC simulator");
#endif
}
