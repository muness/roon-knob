#pragma once

#include <stdbool.h>

/**
 * @brief Initialize battery monitoring
 * @return true on success, false on failure
 */
bool battery_init(void);

/**
 * @brief Get battery voltage in volts
 * @return Battery voltage (3.0V - 4.2V range), or 0.0 on error
 */
float battery_get_voltage(void);

/**
 * @brief Get battery percentage (0-100)
 * @return Battery percentage estimate based on voltage curve
 */
int battery_get_percentage(void);

/**
 * @brief Check if device is charging or on USB power
 * @return true if USB connected/charging, false if on battery
 */
bool battery_is_charging(void);
