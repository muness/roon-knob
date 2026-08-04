#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rk_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if display is currently sleeping
 * @return true if display is off/sleeping, false if on or dimmed
 */
bool platform_display_is_sleeping(void);

/**
 * @brief Set display rotation
 * @param degrees Rotation in degrees (0, 90, 180, 270)
 */
void platform_display_set_rotation(uint16_t degrees);

/** Apply target-specific display power/time-out configuration. */
void platform_display_apply_config(const rk_cfg_t *cfg, bool is_charging);

/**
 * @brief Check if device is charging or on USB power
 * @return true if charging/on USB power, false if on battery
 */
bool platform_battery_is_charging(void);

/**
 * @brief Get battery level percentage
 * @return Battery level 0-100, or -1 if unavailable
 */
int platform_battery_get_level(void);

#ifdef __cplusplus
}
#endif
