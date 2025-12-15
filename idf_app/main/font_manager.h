// Font manager - provides pre-rendered bitmap fonts for Unicode support
// Two font families:
// 1. Text fonts (Charis SIL) - for music metadata with full Unicode support
// 2. Icon fonts (Material Symbols) - for UI controls and status indicators

#pragma once

#include "lvgl.h"
#include <stdbool.h>

// Initialize font manager
// Call after lv_init() but before ui_init()
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
#define ICON_SKIP_PREV      "\xEE\x81\x85"  // U+E045
#define ICON_SKIP_NEXT      "\xEE\x81\x84"  // U+E044

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

// Battery
#define ICON_BATTERY_FULL   "\xEE\x86\xA4"  // U+E1A4
#define ICON_BATTERY_CHARGE "\xEE\x86\xA3"  // U+E1A3 (battery_charging_full)
#define ICON_BATTERY_ALERT  "\xEE\x86\x9C"  // U+E19C (battery_alert - low battery)

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
