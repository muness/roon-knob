#ifndef DISPLAY_SLEEP_H
#define DISPLAY_SLEEP_H

#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rk_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display states: Normal -> Art Mode -> Dim -> Sleep
typedef enum {
    DISPLAY_STATE_NORMAL,    // Full brightness, all controls visible
    DISPLAY_STATE_ART_MODE,  // Full brightness, controls hidden (art focus)
    DISPLAY_STATE_DIM,       // Reduced brightness, controls hidden
    DISPLAY_STATE_SLEEP,     // Screen off
} display_state_t;

/**
 * @brief Initialize display sleep/dim functionality
 *
 * @param panel_handle LCD panel handle for power control
 * @param lvgl_task_handle LVGL task handle for priority control (can be NULL)
 */
void display_sleep_init(esp_lcd_panel_handle_t panel_handle, TaskHandle_t lvgl_task_handle);

/**
 * @brief Enter art mode - hide controls, keep full brightness
 */
void display_art_mode(void);

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

/**
 * @brief Get current display state
 */
display_state_t display_get_state(void);

/**
 * @brief Process pending display state changes
 * Call this from the UI loop to safely handle timer-triggered state changes
 */
void display_process_pending(void);

/**
 * @brief Update dim/sleep timeouts from config
 * Call this when config changes or charging state changes
 * @param cfg Pointer to config (can be NULL to use Kconfig defaults)
 * @param is_charging Current charging state
 */
void display_update_timeouts(const rk_cfg_t *cfg, bool is_charging);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_SLEEP_H
