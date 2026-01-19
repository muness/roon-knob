#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

// Visibility modes
typedef enum {
    VIS_ALWAYS = 0,     // Always visible
    VIS_NEVER = 1,      // Always hidden
    VIS_ON_CHANGE = 2,  // Show on event, fade after timeout
} visibility_mode_t;

// Font sizes (only two available)
typedef enum {
    FONT_SMALL = 0,     // 22px (lato_22)
    FONT_LARGE = 1,     // 28px (notosans_28)
} font_size_t;

// Config for text elements (volume_text, line1, line2, zone)
typedef struct {
    visibility_mode_t visibility;
    font_size_t size;
    uint32_t color;              // RGB hex (0xfafafa)
    uint16_t fade_timeout_ms;    // For VIS_ON_CHANGE (0 = use default 3000ms)
} text_element_config_t;

// Config for arc elements (volume_arc, progress_arc)
typedef struct {
    visibility_mode_t visibility;
    uint32_t color;              // Indicator color
    uint16_t fade_timeout_ms;    // For VIS_ON_CHANGE
} arc_element_config_t;

// Complete display config
typedef struct {
    text_element_config_t volume_text;
    text_element_config_t line1;
    text_element_config_t line2;
    text_element_config_t zone;
    arc_element_config_t volume_arc;
    arc_element_config_t progress_arc;
} display_config_t;

// Get default config (matches current hardcoded behavior)
const display_config_t* display_config_get_default(void);

// Parse display_config from JSON object. Returns true on success.
// On failure, out is unchanged.
bool display_config_parse_json(const cJSON *json, display_config_t *out);

#endif // DISPLAY_CONFIG_H
