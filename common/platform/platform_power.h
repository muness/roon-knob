#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One coherent target power reading for controller policy and telemetry. */
typedef struct {
    int battery_level;       /**< 0-100, or -1 when the target has no gauge. */
    bool external_power;     /**< USB/VBUS/external supply is present. */
} platform_power_snapshot_t;

/**
 * Read battery level and power source together. Target adapters may cache slow
 * ADC/PMIC work; callers should reuse one snapshot for a complete poll cycle.
 */
void platform_power_snapshot(platform_power_snapshot_t *out);

#ifdef __cplusplus
}
#endif
