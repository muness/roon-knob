// Font manager - provides pre-rendered bitmap fonts for Unicode support
// Two font families:
// 1. Text fonts (Charis SIL) - for music metadata with full Unicode support
// 2. Icon fonts (Material Symbols) - for UI controls and status indicators

#pragma once

#include "lvgl.h"
#include <stdbool.h>

// Initialize font manager
// Call after lv_init() but before manifest_ui_init()
bool font_manager_init(void);

// Text fonts (Charis SIL) - for artist, track, album, metadata
const lv_font_t *font_manager_get_small(void);   // 22px for metadata, hints
const lv_font_t *font_manager_get_normal(void);  // 28px for artist, zone names
const lv_font_t *font_manager_get_large(void);   // 40px for track title

// Icon fonts (Material Symbols) - for UI controls
const lv_font_t *font_manager_get_icon_small(void);   // 22px for status icons
const lv_font_t *font_manager_get_icon_normal(void);  // 28px for secondary buttons
const lv_font_t *font_manager_get_icon_large(void);   // 48px for primary buttons

// Check font availability
bool font_manager_has_unicode(void);  // Text fonts loaded?
bool font_manager_has_icons(void);    // Icon fonts loaded?

// Material Icons codepoints (from MaterialIcons-Regular.ttf)
// See: https://github.com/google/material-design-icons/blob/master/font/MaterialIcons-Regular.codepoints

// Media controls
#define ICON_PLAY           "\xEE\x80\xB7"  // U+E037
#define ICON_PAUSE          "\xEE\x80\xB4"  // U+E034
#define ICON_STOP           "\xEE\x81\x87"  // U+E047
#define ICON_SKIP_PREV      "\xEE\x81\x85"  // U+E045
#define ICON_SKIP_NEXT      "\xEE\x81\x84"  // U+E044
#define ICON_SHUFFLE        "\xEE\x81\x83"  // U+E043
#define ICON_REPEAT         "\xEE\x81\x80"  // U+E040
#define ICON_REPEAT_ONE     "\xEE\x81\x81"  // U+E041
#define ICON_FORWARD_5      "\xEE\x81\x98"  // U+E058
#define ICON_FORWARD_10     "\xEE\x81\x96"  // U+E056
#define ICON_FORWARD_30     "\xEE\x81\x97"  // U+E057
#define ICON_REPLAY_5       "\xEE\x81\x9B"  // U+E05B
#define ICON_REPLAY_10      "\xEE\x81\x99"  // U+E059
#define ICON_REPLAY_30      "\xEE\x81\x9A"  // U+E05A

// Volume
#define ICON_VOLUME_UP      "\xEE\x81\x90"  // U+E050
#define ICON_VOLUME_DOWN    "\xEE\x81\x8D"  // U+E04D
#define ICON_VOLUME_MUTE    "\xEE\x81\x8E"  // U+E04E
#define ICON_VOLUME_OFF     "\xEE\x81\x8F"  // U+E04F

// Network/connectivity
#define ICON_WIFI           "\xEE\x98\xBE"  // U+E63E
#define ICON_WIFI_OFF       "\xEE\x99\x88"  // U+E648
#define ICON_BLUETOOTH      "\xEE\x86\xA7"  // U+E1A7
#define ICON_CAST           "\xEE\x8C\x87"  // U+E307
#define ICON_SPEAKER        "\xEE\x8C\xAD"  // U+E32D

// Battery icons (Lucide horizontal style)
// From lucide.ttf - horizontal battery matches UI aesthetic
//
// CODEPOINT NOTE: ICON_BATTERY_LOW (U+E056) and ICON_BATTERY_MEDIUM (U+E057)
// share the same UTF-8 byte sequences as ICON_FORWARD_10 and ICON_FORWARD_30
// above. This is NOT a rendering bug — the correct glyph is selected by the
// LVGL label's font property, not by the byte string. Battery icons are always
// rendered with font_manager_get_lucide_battery(), which contains the Lucide
// battery glyphs at those codepoints. Transport icons use
// font_manager_get_icon_normal()/font_manager_get_icon_large(), which contain
// the Material Icons forward glyphs at the same codepoints. Do NOT change these
// codepoints to resolve the apparent collision — they are correct for each font.
#define ICON_BATTERY_EMPTY    "\xEE\x81\x93"  // U+E053 battery (empty outline)
#define ICON_BATTERY_CHARGING "\xEE\x81\x94"  // U+E054 battery-charging
#define ICON_BATTERY_FULL     "\xEE\x81\x95"  // U+E055 battery-full
#define ICON_BATTERY_LOW      "\xEE\x81\x96"  // U+E056 battery-low  (same bytes as ICON_FORWARD_10; different font)
#define ICON_BATTERY_MEDIUM   "\xEE\x81\x97"  // U+E057 battery-medium (same bytes as ICON_FORWARD_30; different font)
#define ICON_BATTERY_WARNING  "\xEE\x8E\xAC"  // U+E3AC battery-warning

// Lucide battery font (horizontal icons)
const lv_font_t *font_manager_get_lucide_battery(void);

// Status
#define ICON_CHECK          "\xEE\x97\x8A"  // U+E5CA
#define ICON_CLOSE          "\xEE\x97\x8D"  // U+E5CD
#define ICON_ERROR          "\xEE\x80\x80"  // U+E000
#define ICON_WARNING        "\xEE\x80\x82"  // U+E002
#define ICON_INFO           "\xEE\xA2\x8E"  // U+E88E

// Music/audio
#define ICON_MUSIC_NOTE     "\xEE\x90\x85"  // U+E405

// Settings/system
#define ICON_SETTINGS       "\xEE\xA2\xB8"  // U+E8B8
#define ICON_DOWNLOAD       "\xEF\x82\x90"  // U+F090
#define ICON_REFRESH        "\xEE\x97\x95"  // U+E5D5
#define ICON_HOME           "\xEE\xA2\x8A"  // U+E88A
#define ICON_SEARCH         "\xEE\xA2\xB6"  // U+E8B6

// Navigation
#define ICON_ARROW_BACK     "\xEE\x97\x84"  // U+E5C4
#define ICON_CHEVRON_LEFT   "\xEE\x97\x8B"  // U+E5CB
