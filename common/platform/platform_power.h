#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One coherent target power reading for controller policy and telemetry. */
typedef struct {
    int battery_level;       /**< 0-100, or -1 when the target has no gauge. */
    bool external_power;     /**< USB/VBUS/external supply is present. */
} platform_power_snapshot_t;

typedef enum {
    PLATFORM_POWER_STATE_ACTIVE = 0,
    PLATFORM_POWER_STATE_ART,
    PLATFORM_POWER_STATE_DIM,
    PLATFORM_POWER_STATE_DISPLAY_SLEEP,
    PLATFORM_POWER_STATE_UNKNOWN,
} platform_power_state_t;

enum {
    PLATFORM_POWER_CAP_DISPLAY_SLEEP = 1u << 0,
    PLATFORM_POWER_CAP_SOC_DEEP_SLEEP = 1u << 1,
    PLATFORM_POWER_CAP_BOARD_POWER_OFF = 1u << 2,
    PLATFORM_POWER_CAP_RTC_EVIDENCE = 1u << 3,
    PLATFORM_POWER_CAP_FORCED_TEST = 1u << 4,
    PLATFORM_POWER_CAP_AUXILIARY_SOC = 1u << 5,
};

enum {
    PLATFORM_POWER_PREFLIGHT_WAKE_ARMED = 1u << 0,
    PLATFORM_POWER_PREFLIGHT_BLE_OFF = 1u << 1,
    PLATFORM_POWER_PREFLIGHT_DISPLAY_SAFE = 1u << 2,
    PLATFORM_POWER_PREFLIGHT_OUTPUTS_SAFE = 1u << 3,
    PLATFORM_POWER_PREFLIGHT_WIFI_OFF = 1u << 4,
    PLATFORM_POWER_PREFLIGHT_WAKE_SOURCES_SANITIZED = 1u << 5,
};

typedef enum {
    PLATFORM_POWER_PREFLIGHT_ERROR_NONE = 0,
    PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_CONFIG,
    PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_ACTIVE,
    PLATFORM_POWER_PREFLIGHT_ERROR_BLE,
    PLATFORM_POWER_PREFLIGHT_ERROR_DISPLAY,
    PLATFORM_POWER_PREFLIGHT_ERROR_OUTPUTS,
    PLATFORM_POWER_PREFLIGHT_ERROR_WIFI,
    PLATFORM_POWER_PREFLIGHT_ERROR_POLICY,
} platform_power_preflight_error_t;

/** Shared diagnostics schema; unsupported evidence remains zero/false. */
typedef struct {
    platform_power_snapshot_t power;
    platform_power_state_t state;
    uint32_t capabilities;
    bool policy_known;
    bool wifi_modem_sleep_baseline;
    bool automatic_light_sleep_configured;
    bool deep_sleep_timer_active;
    bool debug_sleep_override_armed;
    uint32_t art_timeout_sec;
    uint32_t dim_timeout_sec;
    uint32_t display_sleep_timeout_sec;
    uint32_t power_off_timeout_sec;
    uint32_t art_transitions;
    uint32_t dim_transitions;
    uint32_t display_sleep_transitions;
    uint32_t runtime_wakes;
    uint32_t debug_sleep_arms;
    uint32_t power_off_attempts;
    uint32_t preflight_completions;
    uint32_t power_off_entries;
    uint32_t hardware_wakes;
    uint32_t last_preflight_flags;
    uint32_t last_preflight_error;
    int reset_reason;
    int wakeup_cause;
    uint64_t uptime_ms;
} platform_power_diagnostics_t;

/**
 * Read battery level and power source together. Target adapters may cache slow
 * ADC/PMIC work; callers should reuse one snapshot for a complete poll cycle.
 */
void platform_power_snapshot(platform_power_snapshot_t *out);

/** Build the common policy/status view, then ask the target to enrich it. */
void platform_power_diagnostics_snapshot(platform_power_diagnostics_t *out);

/** Target enrichment hook used by the shared diagnostics implementation. */
void platform_power_diagnostics_enrich(platform_power_diagnostics_t *out);

/** Arm a target-implemented one-time powered sleep test when supported. */
bool platform_power_debug_arm_sleep(uint32_t delay_sec);

/**
 * Disable automatic Light-sleep and clear inherited wake sources before a
 * target installs the exact wake sources it wants for Deep-sleep.
 */
bool platform_power_prepare_for_deep_sleep(void);

#ifdef __cplusplus
}
#endif
