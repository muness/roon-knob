#ifndef PLATFORM_DISPLAY_IDF_H
#define PLATFORM_DISPLAY_IDF_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Initialize the ESP32-S3 display hardware (SPI, LCD panel, backlight)
// Should be called early in app_main, before lv_init()
bool platform_display_init(void);

// Register LVGL display driver and start LVGL task
// Must be called after lv_init() but before any UI code
bool platform_display_register_lvgl_driver(void);

// Check if display is ready for UI operations
bool platform_display_is_ready(void);

// Initialize display sleep management (auto-dim and sleep after inactivity)
// Must be called after UI task is created
// @param lvgl_task_handle Handle to LVGL/UI task for priority control
void platform_display_init_sleep(TaskHandle_t lvgl_task_handle);

// Process any pending display actions (call from UI loop)
void platform_display_process_pending(void);

// Set display rotation (0, 90, 180, 270 degrees)
// @param degrees Rotation in degrees (0, 90, 180, 270)
void platform_display_set_rotation(uint16_t degrees);

#endif // PLATFORM_DISPLAY_IDF_H
