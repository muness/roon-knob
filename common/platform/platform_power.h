#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the hardware can actually establish about its present supply. */
typedef enum {
    PLATFORM_POWER_SOURCE_UNKNOWN = 0,
    PLATFORM_POWER_SOURCE_BATTERY,
    PLATFORM_POWER_SOURCE_EXTERNAL,
} platform_power_source_t;

/** One coherent target power reading for controller policy and telemetry. */
typedef struct {
    int battery_level;       /**< 0-100, or -1 when the target has no gauge. */
    platform_power_source_t source; /**< Observed source, never a policy guess. */
    bool external_power;     /**< Effective policy: use external-power settings. */
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
    PLATFORM_POWER_CAP_DURABLE_EVIDENCE = 1u << 3,
    /* Compatibility name for the Frame/RLCD/Dial RTC implementation. */
    PLATFORM_POWER_CAP_RTC_EVIDENCE = PLATFORM_POWER_CAP_DURABLE_EVIDENCE,
    PLATFORM_POWER_CAP_FORCED_TEST = 1u << 4,
    PLATFORM_POWER_CAP_AUXILIARY_SOC = 1u << 5,
    /** Target can run the persistent voltage-curve experiment. */
    PLATFORM_POWER_CAP_VOLTAGE_EXPERIMENT = 1u << 6,
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

#define PLATFORM_POWER_TRACE_MAX_EVENTS 8
#define PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES 24

typedef struct {
    bool valid;
    int32_t raw_adc;
    int32_t adc_mv;
    int32_t battery_mv;
    int16_t battery_level;
} platform_power_measurement_t;

typedef enum {
    PLATFORM_POWER_EXPERIMENT_IDLE = 0,
    PLATFORM_POWER_EXPERIMENT_ARMED,
    PLATFORM_POWER_EXPERIMENT_RUNNING,
    PLATFORM_POWER_EXPERIMENT_COMPLETE,
} platform_power_experiment_state_t;

typedef struct {
    uint32_t sequence;
    uint32_t elapsed_sec;
    int64_t unix_time_ms;
    int32_t raw_adc;
    int32_t adc_mv;
    int32_t battery_mv;
    int16_t battery_level;
    int8_t reset_reason;
    int8_t wakeup_cause;
} platform_power_experiment_sample_t;

typedef struct {
    uint64_t experiment_id;
    platform_power_experiment_state_t state;
    uint32_t interval_sec; /**< 0 means external-profiler mode. */
    uint32_t maximum_duration_sec;
    uint32_t elapsed_sec;
    uint32_t next_sleep_sec;
    uint32_t total_samples;
    uint32_t intrusive_wakes;
    bool observer_effect;
    uint8_t sample_count;
    platform_power_experiment_sample_t
        samples[PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES];
} platform_power_experiment_t;

typedef enum {
    PLATFORM_POWER_TRACE_BOOT = 1,
    PLATFORM_POWER_TRACE_ATTEMPT,
    PLATFORM_POWER_TRACE_ERROR,
    PLATFORM_POWER_TRACE_PREFLIGHT,
    PLATFORM_POWER_TRACE_ENTRY,
} platform_power_trace_type_t;

/** One compact, NVS-persisted terminal-power lifecycle event. */
typedef struct {
    uint32_t sequence;
    uint32_t boot_id;
    uint32_t uptime_ms;
    int64_t unix_time_ms; /**< UTC Unix milliseconds, or 0 before sync. */
    uint32_t preflight_flags;
    uint32_t preflight_error;
    int16_t battery_level;
    int8_t reset_reason;
    int8_t wakeup_cause;
    uint8_t type;
    uint8_t reserved[3];
} platform_power_trace_event_t;

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
    uint32_t durable_boots;
    uint32_t durable_brownouts;
    bool terminal_entry_pending;
    bool last_boot_followed_terminal_entry;
    int last_entry_battery_level;
    int reset_reason;
    int wakeup_cause;
    uint64_t uptime_ms;
    uint8_t trace_event_count;
    platform_power_trace_event_t trace_events[PLATFORM_POWER_TRACE_MAX_EVENTS];
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

/** True only where a target has an early, radio/UI-free sampling path. */
bool platform_power_experiment_supported(void);

/** Replace any previous experiment and persist a newly armed run. */
bool platform_power_experiment_arm(uint32_t interval_sec,
                                   uint32_t maximum_duration_sec,
                                   uint64_t *experiment_id_out);

/** Record the production sleep boundary and return its requested timer wake. */
bool platform_power_experiment_note_entry(
    const platform_power_measurement_t *measurement,
    uint32_t *next_sleep_sec_out);

/**
 * Record an early boot before UI/radios. Returns true when the target should
 * immediately re-enter Deep-sleep for the returned interval.
 */
bool platform_power_experiment_note_early_boot(
    const platform_power_measurement_t *measurement,
    uint32_t *next_sleep_sec_out);

bool platform_power_experiment_snapshot(platform_power_experiment_t *out);
bool platform_power_experiment_clear(void);

/**
 * Disable automatic Light-sleep and clear inherited wake sources before a
 * target installs the exact wake sources it wants for Deep-sleep.
 */
bool platform_power_prepare_for_deep_sleep(void);

/**
 * Brownout-safe terminal-power journal. These lifecycle calls write only at
 * boot/sleep boundaries; diagnostics polling is read-only after the one boot
 * record. The NVS record complements RTC evidence and survives complete cell
 * collapse or PMIC rail removal.
 */
void platform_power_evidence_note_boot(void);
void platform_power_evidence_note_attempt(void);
void platform_power_evidence_note_error(uint32_t error);
void platform_power_evidence_note_preflight(uint32_t flags);
void platform_power_evidence_note_entry(int battery_level);
/** Backfill current-boot events after the non-blocking network clock syncs. */
void platform_power_evidence_note_time_sync(int64_t unix_time_ms,
                                            uint64_t uptime_ms);

const char *platform_power_source_name(platform_power_source_t source);

#ifdef __cplusplus
}
#endif
