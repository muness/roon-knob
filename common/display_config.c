#include "display_config.h"
#include <string.h>
#include <stdlib.h>

// Default fade timeout for VIS_ON_CHANGE elements
#define DEFAULT_FADE_TIMEOUT_MS 3000

// Static default config instance matching current UI behavior
// Colors extracted from ui.c:
//   volume_text: 0xfafafa (s_volume_label_large)
//   line1 (artist): 0xaaaaaa (s_artist_label)
//   line2 (track): 0xfafafa (s_track_label)
//   zone: 0xbbbbbb (s_zone_label)
//   volume_arc: 0x5a9fd4 (s_volume_arc indicator)
//   progress_arc: 0x7bb9e8 (s_progress_arc indicator)
static const display_config_t s_default_config = {
    .volume_text = {
        .visibility = VIS_ALWAYS,
        .size = FONT_LARGE,
        .color = 0xfafafa,
        .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS,
    },
    .line1 = {
        .visibility = VIS_ALWAYS,
        .size = FONT_SMALL,
        .color = 0xaaaaaa,
        .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS,
    },
    .line2 = {
        .visibility = VIS_ALWAYS,
        .size = FONT_LARGE,
        .color = 0xfafafa,
        .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS,
    },
    .zone = {
        .visibility = VIS_ALWAYS,
        .size = FONT_SMALL,
        .color = 0xbbbbbb,
        .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS,
    },
    .volume_arc = {
        .visibility = VIS_ALWAYS,
        .color = 0x5a9fd4,
        .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS,
    },
    .progress_arc = {
        .visibility = VIS_ALWAYS,
        .color = 0x7bb9e8,
        .fade_timeout_ms = DEFAULT_FADE_TIMEOUT_MS,
    },
};

const display_config_t* display_config_get_default(void) {
    return &s_default_config;
}

// Parse visibility string to enum
// Returns true if valid, false otherwise (out unchanged on failure)
static bool parse_visibility(const char *str, visibility_mode_t *out) {
    if (!str || !out) return false;

    if (strcmp(str, "always") == 0) {
        *out = VIS_ALWAYS;
        return true;
    }
    if (strcmp(str, "never") == 0) {
        *out = VIS_NEVER;
        return true;
    }
    if (strcmp(str, "on_change") == 0) {
        *out = VIS_ON_CHANGE;
        return true;
    }
    return false;
}

// Parse font size string to enum
// Returns true if valid, false otherwise (out unchanged on failure)
static bool parse_font_size(const char *str, font_size_t *out) {
    if (!str || !out) return false;

    if (strcmp(str, "small") == 0) {
        *out = FONT_SMALL;
        return true;
    }
    if (strcmp(str, "large") == 0) {
        *out = FONT_LARGE;
        return true;
    }
    return false;
}

// Parse color string to uint32
// Accepts "#fafafa" or "fafafa" format
// Returns true if valid, false otherwise (out unchanged on failure)
static bool parse_color(const char *str, uint32_t *out) {
    if (!str || !out) return false;

    // Skip leading '#' if present
    if (*str == '#') {
        str++;
    }

    // Validate length (must be 6 hex chars)
    size_t len = strlen(str);
    if (len != 6) return false;

    // Validate all chars are hex digits
    for (size_t i = 0; i < 6; i++) {
        char c = str[i];
        bool is_hex = (c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F');
        if (!is_hex) return false;
    }

    // Parse hex value
    char *endptr;
    unsigned long val = strtoul(str, &endptr, 16);
    if (*endptr != '\0') return false;

    *out = (uint32_t)val;
    return true;
}

// Parse a text element config from JSON object
// Missing fields use values from default_elem
static void parse_text_element(const cJSON *json, text_element_config_t *out, const text_element_config_t *default_elem) {
    if (!json || !out || !default_elem) return;

    // Start with defaults
    *out = *default_elem;

    // Parse visibility
    cJSON *vis = cJSON_GetObjectItem(json, "visibility");
    if (cJSON_IsString(vis) && vis->valuestring) {
        parse_visibility(vis->valuestring, &out->visibility);
    }

    // Parse font size
    cJSON *size = cJSON_GetObjectItem(json, "size");
    if (cJSON_IsString(size) && size->valuestring) {
        parse_font_size(size->valuestring, &out->size);
    }

    // Parse color
    cJSON *color = cJSON_GetObjectItem(json, "color");
    if (cJSON_IsString(color) && color->valuestring) {
        parse_color(color->valuestring, &out->color);
    }

    // Parse fade timeout
    cJSON *timeout = cJSON_GetObjectItem(json, "fade_timeout_ms");
    if (cJSON_IsNumber(timeout)) {
        int val = (int)timeout->valuedouble;
        if (val >= 0 && val <= 0xFFFF) {
            out->fade_timeout_ms = (uint16_t)val;
        }
    }
}

// Parse an arc element config from JSON object
// Missing fields use values from default_elem
static void parse_arc_element(const cJSON *json, arc_element_config_t *out, const arc_element_config_t *default_elem) {
    if (!json || !out || !default_elem) return;

    // Start with defaults
    *out = *default_elem;

    // Parse visibility
    cJSON *vis = cJSON_GetObjectItem(json, "visibility");
    if (cJSON_IsString(vis) && vis->valuestring) {
        parse_visibility(vis->valuestring, &out->visibility);
    }

    // Parse color
    cJSON *color = cJSON_GetObjectItem(json, "color");
    if (cJSON_IsString(color) && color->valuestring) {
        parse_color(color->valuestring, &out->color);
    }

    // Parse fade timeout
    cJSON *timeout = cJSON_GetObjectItem(json, "fade_timeout_ms");
    if (cJSON_IsNumber(timeout)) {
        int val = (int)timeout->valuedouble;
        if (val >= 0 && val <= 0xFFFF) {
            out->fade_timeout_ms = (uint16_t)val;
        }
    }
}

bool display_config_parse_json(const cJSON *json, display_config_t *out) {
    if (!json || !out) return false;

    // Start with a copy of defaults
    display_config_t cfg = s_default_config;

    // Parse each element if present in JSON
    cJSON *volume_text = cJSON_GetObjectItem(json, "volume_text");
    if (cJSON_IsObject(volume_text)) {
        parse_text_element(volume_text, &cfg.volume_text, &s_default_config.volume_text);
    }

    cJSON *line1 = cJSON_GetObjectItem(json, "line1");
    if (cJSON_IsObject(line1)) {
        parse_text_element(line1, &cfg.line1, &s_default_config.line1);
    }

    cJSON *line2 = cJSON_GetObjectItem(json, "line2");
    if (cJSON_IsObject(line2)) {
        parse_text_element(line2, &cfg.line2, &s_default_config.line2);
    }

    cJSON *zone = cJSON_GetObjectItem(json, "zone");
    if (cJSON_IsObject(zone)) {
        parse_text_element(zone, &cfg.zone, &s_default_config.zone);
    }

    cJSON *volume_arc = cJSON_GetObjectItem(json, "volume_arc");
    if (cJSON_IsObject(volume_arc)) {
        parse_arc_element(volume_arc, &cfg.volume_arc, &s_default_config.volume_arc);
    }

    cJSON *progress_arc = cJSON_GetObjectItem(json, "progress_arc");
    if (cJSON_IsObject(progress_arc)) {
        parse_arc_element(progress_arc, &cfg.progress_arc, &s_default_config.progress_arc);
    }

    // Copy to output
    *out = cfg;
    return true;
}
