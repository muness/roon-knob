#ifndef PLATFORM_DISPLAY_IDF_H
#define PLATFORM_DISPLAY_IDF_H

#include <stdbool.h>

// Initialize the ESP32-S3 display hardware (SPI, LCD panel, backlight)
// Should be called early in app_main, before lv_init()
bool platform_display_init(void);

// Register LVGL display driver and start LVGL task
// Must be called after lv_init() but before any UI code
bool platform_display_register_lvgl_driver(void);

// Check if display is ready for UI operations
bool platform_display_is_ready(void);

#endif // PLATFORM_DISPLAY_IDF_H
