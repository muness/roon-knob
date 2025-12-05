#ifndef DISPLAY_SLEEP_H
#define DISPLAY_SLEEP_H

#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize display sleep/dim functionality
 *
 * @param panel_handle LCD panel handle for power control
 * @param lvgl_task_handle LVGL task handle for priority control (can be NULL)
 */
void display_sleep_init(esp_lcd_panel_handle_t panel_handle, TaskHandle_t lvgl_task_handle);

/**
 * @brief Dim the display backlight
 * Reduces backlight to BACKLIGHT_DIM level
 */
void display_dim(void);

/**
 * @brief Put display to sleep
 * Turns off panel completely and lowers LVGL task priority
 */
void display_sleep(void);

/**
 * @brief Wake up the display and restore normal operation
 * Turns on panel, restores backlight and LVGL task priority
 */
void display_wake(void);

/**
 * @brief Call this when any user activity is detected
 * Should be called from touch, encoder, and button callbacks
 * Wakes display if sleeping and resets inactivity timers
 */
void display_activity_detected(void);

/**
 * @brief Set backlight brightness (0-255)
 * @param brightness PWM duty cycle (0=off, 255=full brightness)
 */
void display_set_backlight(uint8_t brightness);

/**
 * @brief Check if display is currently sleeping
 * @return true if display is off, false if on or dimmed
 */
bool display_is_sleeping(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_SLEEP_H
