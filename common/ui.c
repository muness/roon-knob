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
#include "bridge_client.h"

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
    float volume;
    float volume_min;
    float volume_max;
    bool online;
    float volume_step;

    int seek_position;
    int length;
};

// UI widgets - Blue Knob inspired design
static lv_obj_t *s_track_label;        // Main track name
static lv_obj_t *s_artist_label;       // Artist/album
static lv_obj_t *s_volume_arc;         // Outer arc for volume
static lv_obj_t *s_progress_arc;       // Inner arc for track progress
static lv_obj_t *s_volume_label_large; // Volume display (large, prominent) - primary display
static lv_timer_t *s_volume_emphasis_timer;  // Timer to reset volume emphasis after adjustment
static lv_obj_t *s_status_dot;         // Online/offline indicator
static lv_obj_t *s_battery_icon;       // Battery icon (Material Symbols)
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

// Zone picker - using LVGL list widget (supports per-item icons)
static lv_obj_t *s_zone_picker_overlay;    // Dark background overlay
static lv_obj_t *s_zone_list;              // List widget for zone selection
static bool s_zone_picker_visible = false;
#define MAX_ZONE_PICKER_ZONES 16
#define MAX_ZONE_ID_LEN 48
static char s_zone_picker_ids[MAX_ZONE_PICKER_ZONES][MAX_ZONE_ID_LEN];  // Store zone IDs
static int s_zone_picker_count = 0;
static int s_zone_picker_selected = 0;     // Currently highlighted item
static int s_zone_picker_current = -1;     // Currently active zone (no-op if selected)

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
    .volume = 0.0f,
    .volume_min = -80.0f,
    .volume_max = 0.0f,
    .volume_step = 1.0f,
    .online = false,
    .seek_position = 0,
    .length = 0,
};
static bool s_dirty = true;
static char s_pending_message[128] = "";
static bool s_message_dirty = false;
static bool s_zone_name_dirty = false;
static char s_network_status[128] = "";   // Persistent network status (doesn't auto-clear)
static bool s_network_status_dirty = false;
static ui_input_cb_t s_input_cb;
static char s_last_image_key[128] = "";  // Track last loaded artwork
static float s_last_predicted_volume = -9999.0f;  // Track user's predicted volume for emphasis suppression
#ifdef ESP_PLATFORM
static ui_jpeg_image_t s_artwork_img;  // Decoded RGB565 image for artwork (ESP32)
#else
static char *s_artwork_data = NULL;  // Raw JPEG data for PC simulator
#endif

// Fonts - use pre-rendered bitmap fonts on ESP32, built-in on PC
// Two font families: text (Charis SIL) and icons (Material Symbols)
#if !TARGET_PC
#include "font_manager.h"
// Text fonts for music metadata
static inline const lv_font_t *font_small(void) { return font_manager_get_small(); }
static inline const lv_font_t *font_normal(void) { return font_manager_get_normal(); }
static inline const lv_font_t *font_large(void) { return font_manager_get_large(); }
// Icon fonts for UI controls
static inline const lv_font_t *font_icon_small(void) { return font_manager_get_icon_small(); }
static inline const lv_font_t *font_icon_normal(void) { return font_manager_get_icon_normal(); }
static inline const lv_font_t *font_icon_large(void) { return font_manager_get_icon_large(); }
// Icon aliases (Material Symbols on ESP32)
#define UI_ICON_DOWNLOAD  ICON_DOWNLOAD
#else
// PC fallback - use built-in Montserrat (has LVGL symbols)
static inline const lv_font_t *font_small(void) { return &lv_font_montserrat_20; }
static inline const lv_font_t *font_normal(void) { return &lv_font_montserrat_28; }
static inline const lv_font_t *font_large(void) { return &lv_font_montserrat_48; }
static inline const lv_font_t *font_icon_small(void) { return &lv_font_montserrat_20; }
static inline const lv_font_t *font_icon_normal(void) { return &lv_font_montserrat_28; }
static inline const lv_font_t *font_icon_large(void) { return &lv_font_montserrat_48; }
// Icon aliases (LVGL symbols on PC)
#define UI_ICON_DOWNLOAD  LV_SYMBOL_DOWNLOAD
#endif

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
static void zone_list_item_event_cb(lv_event_t *e);
static void show_status_message(const char *message);
static void clear_status_message_timer_cb(lv_timer_t *timer);
static void update_battery_display(void);
static void battery_poll_timer_cb(lv_timer_t *timer);
static void reset_volume_emphasis_timer_cb(lv_timer_t *timer);
static void emphasize_volume_label(void);

// ============================================================================
// Volume Formatting Helper
// ============================================================================

static inline void format_volume_text(char *buf, size_t len, float volume, float volume_min, float volume_step) {
    float step_abs = volume_step < 0.0f ? -volume_step : volume_step;
    int step_is_fractional = (step_abs - (int)step_abs) > 0.01f;

    if (volume_min < 0.0f) {
        if (step_is_fractional) {
            snprintf(buf, len, "%.1f dB", volume);
        } else {
            snprintf(buf, len, "%.0f dB", volume);
        }
    } else {
        if (step_is_fractional) {
            snprintf(buf, len, "%.1f", volume);
        } else {
            snprintf(buf, len, "%.0f", volume);
        }
    }
}

static inline int calculate_volume_percentage(float volume, float volume_min, float volume_max) {
    float vol_range = volume_max - volume_min;
    if (vol_range < 0.01f) return 0;

    int vol_pct = (int)(((volume - volume_min) * 100.0f) / vol_range);
    if (vol_pct < 0) return 0;
    if (vol_pct > 100) return 100;
    return vol_pct;
}

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

    // Poll battery state every 30 seconds to catch charging changes
    lv_timer_t *battery_timer = lv_timer_create(battery_poll_timer_cb, 30000, NULL);
    if (battery_timer) {
        lv_timer_set_repeat_count(battery_timer, -1);
    } else {
        ESP_LOGE(UI_TAG, "FAILED to create battery poll timer!");
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



    // Status dot - top right (on the outer ring)
    s_status_dot = lv_obj_create(s_ui_container);
    lv_obj_set_size(s_status_dot, 10, 10);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_align(s_status_dot, LV_ALIGN_TOP_RIGHT, -35, 35);

    // Battery indicator - centered above zone selector
    // Icon only - no numeric label needed with 4 discrete states
#if !TARGET_PC
    s_battery_icon = lv_label_create(s_ui_container);
    lv_label_set_text(s_battery_icon, ICON_BATTERY_6_BAR);
    lv_obj_set_style_text_font(s_battery_icon, font_icon_small(), 0);
    lv_obj_set_style_text_color(s_battery_icon, lv_color_hex(0x888888), 0);  // Subtle grey
    // Center horizontally above zone selector (zone is at y=50) - lowered to y=35 for visibility
    lv_obj_align(s_battery_icon, LV_ALIGN_TOP_MID, 0, 35);
#endif

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
    lv_obj_set_style_pad_ver(s_zone_label, 12, 0);  // Expand tap area vertically (GH-90)
    lv_obj_add_event_cb(s_zone_label, zone_label_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_zone_label, zone_label_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    // Add press state visual feedback
    lv_obj_set_style_text_color(s_zone_label, lv_color_hex(0xfafafa), LV_STATE_PRESSED);

    // Volume display - large and prominent (primary use case)
    // Positioned between zone and artist, using larger font
    // Emphasis handled by emphasize_volume_label() when volume changes
    s_volume_label_large = lv_label_create(s_ui_container);
    lv_label_set_text(s_volume_label_large, "-- dB");
    lv_obj_set_style_text_font(s_volume_label_large, font_normal(), 0);  // Larger than before
    lv_obj_set_style_text_color(s_volume_label_large, lv_color_hex(0xfafafa), 0);  // Prominent white
    lv_obj_align(s_volume_label_large, LV_ALIGN_TOP_MID, 0, 85);

    // Track name - positioned just above media controls (buttons are at CENTER + 80)
    // Place track at CENTER - 20 to sit nicely above the buttons
    s_track_label = lv_label_create(s_background);
    lv_obj_set_width(s_track_label, SCREEN_SIZE - 80);
    lv_obj_set_style_text_font(s_track_label, font_normal(), 0);
    lv_obj_set_style_text_align(s_track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_track_label, lv_color_hex(0xfafafa), 0);  // Off-white for primary text
    lv_label_set_long_mode(s_track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);  // Horizontal circular scroll
    lv_obj_set_style_anim_time(s_track_label, 25000, LV_PART_MAIN);  // Scroll speed
    lv_obj_align(s_track_label, LV_ALIGN_CENTER, 0, -20);  // Just above media controls
    lv_label_set_text(s_track_label, s_pending.line1);

    // Artist - positioned above track name with smaller font
    s_artist_label = lv_label_create(s_background);
    lv_obj_set_width(s_artist_label, SCREEN_SIZE - 80);
    lv_obj_set_style_text_font(s_artist_label, font_small(), 0);
    lv_obj_set_style_text_align(s_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_artist_label, lv_color_hex(0xaaaaaa), 0);  // Light grey for secondary text
    lv_label_set_long_mode(s_artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);  // Horizontal circular scroll
    lv_obj_set_style_anim_time(s_artist_label, 25000, LV_PART_MAIN);  // Scroll speed
    lv_obj_align(s_artist_label, LV_ALIGN_CENTER, 0, -55);  // Above track name
    lv_label_set_text(s_artist_label, s_pending.line2);

    // Media control buttons - Blue Knob style (3 circular buttons)
    int btn_y = 80;  // Offset from center - moved lower for better ergonomics
    int btn_spacing = 75;  // Space between buttons (adjusted for larger sizes)

    // Previous button (left) - use lv_btn_create for proper button widget
    s_btn_prev = lv_btn_create(s_background);
    lv_obj_set_size(s_btn_prev, 60, 60);  // Increased from 50x50 for easier tapping
    lv_obj_add_style(s_btn_prev, &style_button_secondary, 0);
    lv_obj_align(s_btn_prev, LV_ALIGN_CENTER, -btn_spacing, btn_y);
    lv_obj_add_event_cb(s_btn_prev, btn_prev_event_cb, LV_EVENT_CLICKED, NULL);

    // Override ALL states to prevent theme colors
    lv_obj_set_style_bg_color(s_btn_prev, lv_color_hex(0x1a1a1a), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_prev, lv_color_hex(0x3c3c3c), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_btn_prev, COLOR_GREY, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_btn_prev, lv_color_hex(0x5a9fd4), LV_STATE_PRESSED);

    lv_obj_t *prev_label = lv_label_create(s_btn_prev);
#if !TARGET_PC
    lv_label_set_text(prev_label, ICON_SKIP_PREV);
    lv_obj_set_style_text_font(prev_label, font_icon_normal(), 0);
#else
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_28, 0);
#endif
    lv_obj_add_style(prev_label, &style_button_label, 0);
    lv_obj_center(prev_label);

    // Play/Pause button (center, larger)
    s_btn_play = lv_btn_create(s_background);
    lv_obj_set_size(s_btn_play, 80, 80);  // Increased from 70x70 for easier tapping
    lv_obj_add_style(s_btn_play, &style_button_primary, 0);
    lv_obj_align(s_btn_play, LV_ALIGN_CENTER, 0, btn_y);
    lv_obj_add_event_cb(s_btn_play, btn_play_event_cb, LV_EVENT_CLICKED, NULL);

    // Override ALL states to prevent theme colors
    lv_obj_set_style_bg_color(s_btn_play, lv_color_hex(0x2c2c2c), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_play, lv_color_hex(0x3c3c3c), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_btn_play, lv_color_hex(0x5a9fd4), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_btn_play, lv_color_hex(0x7bb9e8), LV_STATE_PRESSED);

    s_play_icon = lv_label_create(s_btn_play);
#if !TARGET_PC
    lv_label_set_text(s_play_icon, ICON_PLAY);
    lv_obj_set_style_text_font(s_play_icon, font_icon_large(), 0);
#else
    lv_label_set_text(s_play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(s_play_icon, &lv_font_montserrat_48, 0);
#endif
    lv_obj_add_style(s_play_icon, &style_button_label, 0);
    lv_obj_center(s_play_icon);

    // Next button (right)
    s_btn_next = lv_btn_create(s_background);
    lv_obj_set_size(s_btn_next, 60, 60);  // Increased from 50x50 for easier tapping
    lv_obj_add_style(s_btn_next, &style_button_secondary, 0);
    lv_obj_align(s_btn_next, LV_ALIGN_CENTER, btn_spacing, btn_y);
    lv_obj_add_event_cb(s_btn_next, btn_next_event_cb, LV_EVENT_CLICKED, NULL);

    // Override ALL states to prevent theme colors
    lv_obj_set_style_bg_color(s_btn_next, lv_color_hex(0x1a1a1a), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_next, lv_color_hex(0x3c3c3c), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_btn_next, COLOR_GREY, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_btn_next, lv_color_hex(0x5a9fd4), LV_STATE_PRESSED);

    lv_obj_t *next_label = lv_label_create(s_btn_next);
#if !TARGET_PC
    lv_label_set_text(next_label, ICON_SKIP_NEXT);
    lv_obj_set_style_text_font(next_label, font_icon_normal(), 0);
#else
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_28, 0);
#endif
    lv_obj_add_style(next_label, &style_button_label, 0);
    lv_obj_center(next_label);

    // Status bar at bottom - for transient messages like "Bridge: Connected"
    s_status_bar = lv_label_create(s_ui_container);
    lv_label_set_text(s_status_bar, "");
    lv_obj_set_width(s_status_bar, SCREEN_SIZE - 60);
    lv_obj_set_style_text_font(s_status_bar, font_small(), 0);
    lv_obj_set_style_text_align(s_status_bar, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_status_bar, lv_color_hex(0x000000), 0);  // Black text
    lv_label_set_long_mode(s_status_bar, LV_LABEL_LONG_DOT);
    // Background styling (hidden by default, shown when message appears)
    lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(0xfafafa), 0);  // Off-white
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_TRANSP, 0);  // Hidden initially
    lv_obj_set_style_pad_ver(s_status_bar, 4, 0);
    lv_obj_set_style_pad_hor(s_status_bar, 12, 0);
    lv_obj_set_style_radius(s_status_bar, 8, 0);
    lv_obj_align(s_status_bar, LV_ALIGN_BOTTOM_MID, 0, -25);  // Higher up from edge
}

// ============================================================================
// Event Handlers
// ============================================================================

static bool s_zone_long_pressed = false;  // Prevent click after long press

static void zone_label_event_cb(lv_event_t *e) {
    (void)e;
    // Skip click if it came from a long press release
    if (s_zone_long_pressed) {
        s_zone_long_pressed = false;
        return;
    }
    if (s_input_cb) {
        s_input_cb(UI_INPUT_MENU);
    }
}

static void zone_label_long_press_cb(lv_event_t *e) {
    (void)e;
    s_zone_long_pressed = true;  // Mark that we handled a long press
    ui_show_settings();
}

static void btn_prev_event_cb(lv_event_t *e) {
    (void)e;
    ESP_LOGI(UI_TAG, "btn_prev_event_cb triggered, s_input_cb=%p", (void*)s_input_cb);
    if (s_input_cb) {
        s_input_cb(UI_INPUT_PREV_TRACK);
    }
}

static void btn_play_event_cb(lv_event_t *e) {
    (void)e;
    ESP_LOGI(UI_TAG, "btn_play_event_cb triggered, s_input_cb=%p", (void*)s_input_cb);
    if (s_input_cb) {
        s_input_cb(UI_INPUT_PLAY_PAUSE);
    }
}

static void btn_next_event_cb(lv_event_t *e) {
    (void)e;
    ESP_LOGI(UI_TAG, "btn_next_event_cb triggered, s_input_cb=%p", (void*)s_input_cb);
    if (s_input_cb) {
        s_input_cb(UI_INPUT_NEXT_TRACK);
    }
}

static void zone_list_item_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);
    s_zone_picker_selected = index;
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

    // Update volume arc and label, emphasize if volume changed
    // Volume is in dB with zone-specific min/max range
    static float last_volume = -9999.0f;  // Sentinel value (unlikely real volume)
    static bool volume_initialized = false;
    float vol_diff = state->volume < last_volume ? last_volume - state->volume : state->volume - last_volume;
    if (volume_initialized && vol_diff > 0.01f) {
        // Only emphasize if value differs from last user prediction
        // (suppresses redundant emphasis when poll confirms user's change)
        float pred_diff = state->volume < s_last_predicted_volume ? s_last_predicted_volume - state->volume : state->volume - s_last_predicted_volume;
        if (pred_diff > 0.01f) {
            emphasize_volume_label();
        }
    }
    volume_initialized = true;
    last_volume = state->volume;

    // Convert to 0-100 scale for arc display using zone's actual min/max
    int vol_pct = calculate_volume_percentage(state->volume, state->volume_min, state->volume_max);
    lv_arc_set_value(s_volume_arc, vol_pct);

    // Display volume (format matches zone's step precision)
    char vol_text[16];
    // Note: volume_min is atomic float read; no lock needed (self-corrects on next poll if stale)
    format_volume_text(vol_text, sizeof(vol_text), state->volume, state->volume_min, state->volume_step);
    lv_label_set_text(s_volume_label_large, vol_text);

    // Update progress arc based on seek position and track length
    if (s_progress_arc && state->length > 0) {
        int progress_pct = (state->seek_position * 100) / state->length;
        if (progress_pct > 100) progress_pct = 100;
        if (progress_pct < 0) progress_pct = 0;
        lv_arc_set_value(s_progress_arc, progress_pct);
        lv_obj_invalidate(s_progress_arc);
    } else if (s_progress_arc) {
        lv_arc_set_value(s_progress_arc, 0);
        lv_obj_invalidate(s_progress_arc);
    }

    // Update play/pause icon
    if (s_play_icon) {
#if !TARGET_PC
        lv_label_set_text(s_play_icon, state->playing ? ICON_PAUSE : ICON_PLAY);
#else
        lv_label_set_text(s_play_icon, state->playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
#endif
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

    bool network_status_changed = s_network_status_dirty;
    char net_status[128];
    if (network_status_changed) {
        strncpy(net_status, s_network_status, sizeof(net_status) - 1);
        net_status[sizeof(net_status) - 1] = '\0';
        s_network_status_dirty = false;
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
    if (network_status_changed && s_status_bar) {
        // Set network status directly without auto-clear timer
        lv_label_set_text(s_status_bar, net_status);
        // Show/hide background based on content
        lv_obj_set_style_bg_opa(s_status_bar, net_status[0] ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        // Cancel any pending auto-clear from show_status_message
        if (s_status_timer) {
            lv_timer_del(s_status_timer);
            s_status_timer = NULL;
        }
    }
}

static int s_last_battery_level = -1;  // 0-3 levels for hysteresis
static bool s_last_battery_charging = false;  // Track charging state changes

static void update_battery_display(void) {
#ifdef ESP_PLATFORM
    if (!s_battery_icon) return;

    int percent = battery_get_percentage();
    bool charging = battery_is_charging();

    // Convert to 4 discrete levels for stability (precision matches fidelity)
    // Critical: ≤10%, Low: 11-25%, Medium: 26-60%, High: ≥61%
    int level;
    if (percent <= 10) level = 0;       // Critical
    else if (percent <= 25) level = 1;  // Low
    else if (percent <= 60) level = 2;  // Medium
    else level = 3;                     // High

    // Only update display if level or charging state changed (prevents flicker)
    if (level == s_last_battery_level && charging == s_last_battery_charging) {
        return;
    }

    s_last_battery_level = level;
    s_last_battery_charging = charging;

    // Update battery icon based on state
    // Uses consistent 0/2/4/6-bar progression for clear visual distinction
    lv_obj_clear_flag(s_battery_icon, LV_OBJ_FLAG_HIDDEN);
    if (charging) {
        lv_label_set_text(s_battery_icon, ICON_BATTERY_CHARGE);
    } else {
        switch (level) {
            case 0:  lv_label_set_text(s_battery_icon, ICON_BATTERY_0_BAR); break;  // Critical
            case 1:  lv_label_set_text(s_battery_icon, ICON_BATTERY_2_BAR); break;  // Low
            case 2:  lv_label_set_text(s_battery_icon, ICON_BATTERY_4_BAR); break;  // Medium
            default: lv_label_set_text(s_battery_icon, ICON_BATTERY_6_BAR); break;  // High
        }
    }

    // Warning color for critical/low battery, neutral grey otherwise
    if (level <= 1 && !charging) {
        lv_obj_set_style_text_color(s_battery_icon, lv_color_hex(0xff0000), 0);
    } else {
        lv_obj_set_style_text_color(s_battery_icon, lv_color_hex(0x888888), 0);
    }
#endif
}

static void battery_poll_timer_cb(lv_timer_t *timer) {
    (void)timer;
    update_battery_display();
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
    // Show background when message is visible
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_COVER, 0);

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
        // Hide background when empty
        lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_TRANSP, 0);
    }
    s_status_timer = NULL;
}

// ============================================================================
// Volume Emphasis - Highlights volume display when adjusting
// ============================================================================

static void reset_volume_emphasis_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_volume_label_large) {
        lv_obj_set_style_text_color(s_volume_label_large, lv_color_hex(0xfafafa), 0);  // Reset to white
    }
    s_volume_emphasis_timer = NULL;
}

static void emphasize_volume_label(void) {
    if (!s_volume_label_large) {
        return;
    }

    // Emphasize with bright blue
    lv_obj_set_style_text_color(s_volume_label_large, lv_color_hex(0x7bb9e8), 0);

    // Reset/create timer to remove emphasis after 1.5 seconds
    if (s_volume_emphasis_timer) {
        lv_timer_reset(s_volume_emphasis_timer);
    } else {
        s_volume_emphasis_timer = lv_timer_create(reset_volume_emphasis_timer_cb, 1500, NULL);
        lv_timer_set_repeat_count(s_volume_emphasis_timer, 1);
    }
}

// ============================================================================
// Zone Picker - LVGL List Widget (supports per-item icons)
// ============================================================================

// Special zone IDs (must match bridge_client.c)
#define ZONE_ID_BACK "__back__"
#define ZONE_ID_SETTINGS "__settings__"

void ui_show_zone_picker(const char **zone_names, const char **zone_ids, int count, int selected) {
    if (s_zone_picker_visible) {
        return;
    }

    // Store zone IDs for later retrieval
    s_zone_picker_count = (count > MAX_ZONE_PICKER_ZONES) ? MAX_ZONE_PICKER_ZONES : count;
    s_zone_picker_selected = selected;
    s_zone_picker_current = selected;  // Remember current zone for no-op detection
    for (int i = 0; i < s_zone_picker_count; i++) {
        strncpy(s_zone_picker_ids[i], zone_ids[i], MAX_ZONE_ID_LEN - 1);
        s_zone_picker_ids[i][MAX_ZONE_ID_LEN - 1] = '\0';
    }

    // Create fullscreen dark overlay
    s_zone_picker_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_zone_picker_overlay, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_center(s_zone_picker_overlay);
    lv_obj_set_style_bg_color(s_zone_picker_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_zone_picker_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_zone_picker_overlay, 0, 0);
    lv_obj_set_style_radius(s_zone_picker_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_zone_picker_overlay, 0, 0);

    // Title at top
    lv_obj_t *title = lv_label_create(s_zone_picker_overlay);
    lv_label_set_text(title, "SELECT ZONE");
    lv_obj_set_style_text_font(title, font_normal(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xfafafa), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Create list widget - allows per-item icons
    s_zone_list = lv_list_create(s_zone_picker_overlay);
    lv_obj_set_size(s_zone_list, SCREEN_SIZE - 60, SCREEN_SIZE - 120);
    lv_obj_align(s_zone_list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_zone_list, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(s_zone_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_zone_list, 0, 0);
    lv_obj_set_style_pad_all(s_zone_list, 0, 0);
    lv_obj_set_style_radius(s_zone_list, 10, 0);

    // Add items to list with appropriate icons
    for (int i = 0; i < s_zone_picker_count; i++) {
        lv_obj_t *btn = lv_list_add_btn(s_zone_list, NULL, NULL);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x3a3a3a), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_ver(btn, 18, 0);   // Larger vertical padding for easier tapping
        lv_obj_set_style_pad_hor(btn, 16, 0);
        lv_obj_set_height(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(btn, 56, 0);  // Minimum 56px tap target (Material Design guideline)

        // Use flex layout for proper vertical alignment of icon and text
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, 12, 0);  // Gap between icon and text

        // Highlight current selection
        if (i == selected) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a4a6a), 0);
        }

        // Create icon label (using icon font)
        lv_obj_t *icon_label = lv_label_create(btn);
#if !TARGET_PC
        lv_obj_set_style_text_font(icon_label, font_icon_small(), 0);
        // Icon font glyphs sit high in their bounding box - add top padding to center visually
        lv_obj_set_style_pad_top(icon_label, 4, 0);
#else
        lv_obj_set_style_text_font(icon_label, font_normal(), 0);
#endif
        lv_obj_set_style_text_color(icon_label, lv_color_hex(0xaaaaaa), 0);

        // Set icon based on zone type
        const char *zone_id = zone_ids[i];
        if (strcmp(zone_id, ZONE_ID_BACK) == 0) {
#if !TARGET_PC
            lv_label_set_text(icon_label, ICON_ARROW_BACK);
#else
            lv_label_set_text(icon_label, LV_SYMBOL_LEFT);
#endif
        } else if (strcmp(zone_id, ZONE_ID_SETTINGS) == 0) {
#if !TARGET_PC
            lv_label_set_text(icon_label, ICON_SETTINGS);
#else
            lv_label_set_text(icon_label, LV_SYMBOL_SETTINGS);
#endif
        } else {
            // Regular zone - use music note icon
#if !TARGET_PC
            lv_label_set_text(icon_label, ICON_MUSIC_NOTE);
#else
            lv_label_set_text(icon_label, LV_SYMBOL_AUDIO);
#endif
        }

        // Create text label (using text font)
        lv_obj_t *text_label = lv_label_create(btn);
        lv_obj_set_style_text_font(text_label, font_normal(), 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0xfafafa), 0);
        lv_label_set_text(text_label, zone_names[i]);

        // Store index in button user data
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, zone_list_item_event_cb, LV_EVENT_CLICKED, NULL);
    }

    // Scroll to selected item
    if (selected > 0 && selected < s_zone_picker_count) {
        lv_obj_t *selected_btn = lv_obj_get_child(s_zone_list, selected);
        if (selected_btn) {
            lv_obj_scroll_to_view(selected_btn, LV_ANIM_OFF);
        }
    }

    // Hint text at bottom
    lv_obj_t *hint = lv_label_create(s_zone_picker_overlay);
    lv_label_set_text(hint, "Tap to select");
    lv_obj_set_style_text_font(hint, font_small(), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
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
        s_zone_list = NULL;
    }
    s_zone_picker_visible = false;
}

int ui_get_zone_picker_selected(void) {
    if (!s_zone_picker_visible) {
        return -1;
    }
    return s_zone_picker_selected;
}

void ui_zone_picker_get_selected_id(char *out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (!s_zone_picker_visible) {
        return;
    }
    int selected = s_zone_picker_selected;
    if (selected >= 0 && selected < s_zone_picker_count) {
        strncpy(out, s_zone_picker_ids[selected], len - 1);
        out[len - 1] = '\0';
    }
}

bool ui_zone_picker_is_current_selection(void) {
    // Returns true if user selected the same zone they started with (no-op)
    return s_zone_picker_selected == s_zone_picker_current;
}

// ============================================================================
// Public API
// ============================================================================

void ui_loop_iter(void) {
    lv_task_handler();
    lv_timer_handler();

    platform_task_run_pending();  // Process callbacks from bridge_client thread

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

void ui_set_volume_with_range(float vol, float vol_min, float vol_max, float vol_step) {
    os_mutex_lock(&s_state_lock);
    s_pending.volume = vol;
    s_pending.volume_min = vol_min;
    s_pending.volume_max = vol_max;
    s_pending.volume_step = vol_step;
    s_dirty = true;
    os_mutex_unlock(&s_state_lock);
}

void ui_show_volume_change(float vol, float vol_step) {
    s_last_predicted_volume = vol;  // Track prediction for emphasis suppression

    // Note: Reading volume_min/volume_max without lock (atomic float reads, self-correct on next poll if stale)

    // Update volume arc immediately (optimistic)
    if (s_volume_arc) {
        int vol_pct = calculate_volume_percentage(vol, s_pending.volume_min, s_pending.volume_max);
        lv_arc_set_value(s_volume_arc, vol_pct);
    }

    // Update volume label immediately (optimistic)
    char vol_text[16];
    format_volume_text(vol_text, sizeof(vol_text), vol, s_pending.volume_min, vol_step);

    if (s_volume_label_large) {
        lv_label_set_text(s_volume_label_large, vol_text);
        emphasize_volume_label();
    }
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

void ui_set_network_status(const char *status) {
    os_mutex_lock(&s_state_lock);
    if (status) {
        strncpy(s_network_status, status, sizeof(s_network_status) - 1);
        s_network_status[sizeof(s_network_status) - 1] = '\0';
    } else {
        s_network_status[0] = '\0';  // Clear status
    }
    s_network_status_dirty = true;
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

void ui_handle_volume_rotation(int ticks) {
    if (ui_is_zone_picker_visible()) {
        // Scroll zone picker instead of changing volume
        ui_zone_picker_scroll(ticks > 0 ? 1 : -1);
    } else {
        // Dispatch velocity-sensitive volume rotation to bridge_client
        bridge_client_handle_volume_rotation(ticks);
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

void ui_update(const char *line1, const char *line2, bool playing, float volume, float volume_min, float volume_max, float volume_step, int seek_position, int length) {
    ui_set_track(line1, line2);
    ui_set_playing(playing);
    ui_set_volume_with_range(volume, volume_min, volume_max, volume_step);
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
    if (!bridge_client_get_artwork_url(url, sizeof(url), SCREEN_SIZE, SCREEN_SIZE)) {
        ESP_LOGW(UI_TAG, "Failed to build artwork URL");
        return;
    }

    ESP_LOGI(UI_TAG, "Fetching artwork: %s", url);

    // Fetch image data (JPEG)
    char *img_data = NULL;
    size_t img_len = 0;
    int ret = platform_http_get_image(url, &img_data, &img_len);

    if (ret != 0 || !img_data || img_len == 0) {
        ESP_LOGW(UI_TAG, "Failed to fetch artwork (ret=%d, len=%zu)", ret, img_len);
        platform_http_free(img_data);
        lv_obj_add_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    ESP_LOGI(UI_TAG, "Artwork fetched: %zu bytes", img_len);

#ifdef ESP_PLATFORM
    // Expect raw RGB565 data from bridge (format=rgb565)
    // Expected size: width * height * 2 bytes
    const size_t expected_rgb565_size = SCREEN_SIZE * SCREEN_SIZE * 2;
    if (img_len != expected_rgb565_size) {
        ESP_LOGW(UI_TAG, "Unexpected image size: %zu bytes (expected %zu for %dx%d RGB565)",
                 img_len, expected_rgb565_size, SCREEN_SIZE, SCREEN_SIZE);
        platform_http_free(img_data);
        lv_obj_add_flag(s_artwork_image, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    ESP_LOGI(UI_TAG, "Processing raw RGB565 format (%zu bytes)", img_len);

    // Copy to global buffer (maintains ownership model)
    ui_jpeg_image_t new_img;
    bool ok = ui_rgb565_from_buffer((const uint8_t *)img_data,
                                    SCREEN_SIZE, SCREEN_SIZE, &new_img);

    // HTTP buffer no longer needed after copy
    platform_http_free(img_data);

    if (!ok) {
        ESP_LOGW(UI_TAG, "Failed to process RGB565 data");
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
    if (!s_zone_picker_visible || !s_zone_list || s_zone_picker_count == 0) {
        return;
    }

    // Calculate new position with wraparound
    int new_pos = s_zone_picker_selected + delta;
    if (new_pos < 0) new_pos = s_zone_picker_count - 1;
    if (new_pos >= s_zone_picker_count) new_pos = 0;

    // Update visual selection
    if (new_pos != s_zone_picker_selected) {
        // Remove highlight from old selection
        lv_obj_t *old_btn = lv_obj_get_child(s_zone_list, s_zone_picker_selected);
        if (old_btn) {
            lv_obj_set_style_bg_color(old_btn, lv_color_hex(0x1a1a1a), 0);
        }

        // Add highlight to new selection
        lv_obj_t *new_btn = lv_obj_get_child(s_zone_list, new_pos);
        if (new_btn) {
            lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x2a4a6a), 0);
            lv_obj_scroll_to_view(new_btn, LV_ANIM_ON);
        }

        s_zone_picker_selected = new_pos;
    }
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

        // Hide settings panel so update button is visible
        ui_hide_settings();

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
                snprintf(text, sizeof(text), UI_ICON_DOWNLOAD " Update to %s", s_update_version);
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

// ============================================================================
// Display State Control - Art Mode
// ============================================================================

void ui_set_controls_visible(bool visible) {
    if (visible) {
        // Show all controls
        if (s_btn_prev) lv_obj_clear_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
        if (s_btn_play) lv_obj_clear_flag(s_btn_play, LV_OBJ_FLAG_HIDDEN);
        if (s_btn_next) lv_obj_clear_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
        if (s_track_label) lv_obj_clear_flag(s_track_label, LV_OBJ_FLAG_HIDDEN);
        if (s_artist_label) lv_obj_clear_flag(s_artist_label, LV_OBJ_FLAG_HIDDEN);
        if (s_zone_label) lv_obj_clear_flag(s_zone_label, LV_OBJ_FLAG_HIDDEN);
        if (s_volume_label_large) lv_obj_clear_flag(s_volume_label_large, LV_OBJ_FLAG_HIDDEN);
        if (s_battery_icon) lv_obj_clear_flag(s_battery_icon, LV_OBJ_FLAG_HIDDEN);
        if (s_status_dot) lv_obj_clear_flag(s_status_dot, LV_OBJ_FLAG_HIDDEN);
        if (s_status_bar) lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_HIDDEN);
        // Restore artwork dimming for text contrast
        if (s_artwork_image) lv_obj_set_style_img_opa(s_artwork_image, LV_OPA_40, 0);
        ESP_LOGI(UI_TAG, "Controls shown");
        // Force battery display update after showing controls (GH-86)
        // Without this, hysteresis in update_battery_display() prevents the icon from reappearing
        s_last_battery_level = -1;
        update_battery_display();
    } else {
        // Hide controls for art mode - show only artwork and arcs
        if (s_btn_prev) lv_obj_add_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
        if (s_btn_play) lv_obj_add_flag(s_btn_play, LV_OBJ_FLAG_HIDDEN);
        if (s_btn_next) lv_obj_add_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
        if (s_track_label) lv_obj_add_flag(s_track_label, LV_OBJ_FLAG_HIDDEN);
        if (s_artist_label) lv_obj_add_flag(s_artist_label, LV_OBJ_FLAG_HIDDEN);
        if (s_zone_label) lv_obj_add_flag(s_zone_label, LV_OBJ_FLAG_HIDDEN);
        if (s_volume_label_large) lv_obj_add_flag(s_volume_label_large, LV_OBJ_FLAG_HIDDEN);
        if (s_battery_icon) lv_obj_add_flag(s_battery_icon, LV_OBJ_FLAG_HIDDEN);
        if (s_status_dot) lv_obj_add_flag(s_status_dot, LV_OBJ_FLAG_HIDDEN);
        if (s_status_bar) lv_obj_add_flag(s_status_bar, LV_OBJ_FLAG_HIDDEN);
        // Make artwork fully visible in art mode
        if (s_artwork_image) lv_obj_set_style_img_opa(s_artwork_image, LV_OPA_COVER, 0);
        ESP_LOGI(UI_TAG, "Controls hidden (art mode)");
    }
}

