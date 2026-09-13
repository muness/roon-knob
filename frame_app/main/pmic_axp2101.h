#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize AXP2101 PMIC via I2C
// SDA=47, SCL=48 (PhotoPainter board), address=0x34
bool pmic_init(void);

typedef enum {
    PMIC_POWER_SOURCE_UNKNOWN = 0,
    PMIC_POWER_SOURCE_BATTERY,
    PMIC_POWER_SOURCE_EXTERNAL,
} pmic_power_source_t;

// Distinguish readable VBUS, readable battery operation, and I2C uncertainty.
pmic_power_source_t pmic_power_source(void);

// Legacy shared-platform projection: true means external VBUS is present.
bool pmic_is_charging(void);

// Get battery percentage (0-100), or -1 if unavailable
int pmic_get_battery_percent(void);

// Get battery voltage in mV
int pmic_get_battery_voltage(void);

// Remove power from the e-paper peripheral rails immediately before S3 sleep.
bool pmic_prepare_for_deep_sleep(void);
