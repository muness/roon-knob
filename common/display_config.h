#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <cJSON.h>

// Visibility modes
typedef enum {
    VIS_ALWAYS = 0,     // Always visible
    VIS_NEVER = 1,      // Always hidden
    VIS_ON_CHANGE = 2,  // Show on event, fade after timeout
} visibility_mode_t;

// Font sizes (three available)
typedef enum font_size {
    FONT_SMALL = 0,     // 22px
    FONT_MEDIUM = 1,    // 25px
    FONT_LARGE = 2,     // 28px
} font_size_t;

// Font families
typedef enum font_family {
    FONT_LATO = 0,      // Lato (clean sans-serif)
    FONT_NOTOSANS = 1,  // Noto Sans (humanist sans-serif)
} font_family_t;

// Text alignment
typedef enum {
    TEXT_ALIGN_CENTER = 0,  // Center (default)
    TEXT_ALIGN_LEFT = 1,    // Left aligned
    TEXT_ALIGN_RIGHT = 2,   // Right aligned
} text_align_t;

// Config for text elements (volume_text, line1, line2, zone)
typedef struct {
    visibility_mode_t visibility;
    font_size_t size;
    font_family_t family;        // Font family (lato or notosans)
    text_align_t align;          // Text alignment (center, left, right)
    uint32_t color;              // RGB hex (0xfafafa)
    uint16_t fade_timeout_ms;    // For VIS_ON_CHANGE (0 = use default 3000ms)
} text_element_config_t;

// Config for arc elements (volume_arc, progress_arc)
typedef struct {
    visibility_mode_t visibility;
    uint32_t color;              // Indicator color
    uint8_t width;               // Arc width in pixels (0 = use default 6px)
    uint16_t fade_timeout_ms;    // For VIS_ON_CHANGE (0 = use default 3000ms)
} arc_element_config_t;

// Icon sizes for buttons (Material Icons font sizes)
typedef enum {
    ICON_SIZE_NORMAL = 0,   // 44px (for secondary buttons)
    ICON_SIZE_LARGE = 1,    // 60px (for primary play/pause button)
} icon_size_t;

// Config for transport buttons (prev, play_pause, next)
typedef struct {
    visibility_mode_t visibility;
    uint32_t icon_color;         // Icon color (0xfafafa default)
    uint32_t bg_color;           // Background color (0x1a1a1a secondary, 0x2c2c2c primary)
    uint32_t border_color;       // Border color (0x4a4a4a secondary, 0x5a9fd4 primary)
    icon_size_t icon_size;       // Icon font size (normal=44px, large=60px)
} button_config_t;

// Complete display config
typedef struct display_config {
    text_element_config_t volume_text;
    text_element_config_t line1;
    text_element_config_t line2;
    text_element_config_t zone;
    arc_element_config_t volume_arc;
    arc_element_config_t progress_arc;
    button_config_t prev_button;
    button_config_t play_button;
    button_config_t next_button;
} display_config_t;

// Get default config (matches current hardcoded behavior)
const display_config_t* display_config_get_default(void);

// Parse display_config from JSON object. Returns true on success.
// On failure, out is unchanged.
bool display_config_parse_json(const cJSON *json, display_config_t *out);

#endif // DISPLAY_CONFIG_H
