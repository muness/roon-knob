#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize AXP2101 PMIC via I2C
// SDA=6, SCL=7 (PhotoPainter board), address=0x34
bool pmic_init(void);

// Check if battery is charging
bool pmic_is_charging(void);

// Get battery percentage (0-100), or -1 if unavailable
int pmic_get_battery_percent(void);

// Get battery voltage in mV
int pmic_get_battery_voltage(void);
