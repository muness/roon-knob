#pragma once

// M5Stack Tough (ILI9342C + CHSC6540) display/touch bring-up. See
// docs/esp/hw-reference/board-tough.md for the sourced hardware facts this
// implementation is based on (AXP192-mediated reset/backlight, CHSC6540
// register protocol).

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the AXP192 power sequencing, SPI bus, ILI9342C panel, and I2C
// touch controller. Must be called before lv_init().
bool platform_display_init(void);

// Register the LVGL display flush callback and touch pointer indev. Must be
// called after lv_init() but before any UI code runs.
bool platform_display_register_lvgl_driver(void);

bool platform_display_is_ready(void);

#ifdef __cplusplus
}
#endif
