#pragma once

#include "rk_cfg.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTROLLER_CONFIG_DURABILITY_UNINITIALIZED = 0,
    CONTROLLER_CONFIG_DURABILITY_DURABLE,
    CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT,
    CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY,
} controller_config_durability_t;

/*
 * Public transaction outcome. Storage transport details stay private to the
 * configuration owner and its target backend.
 */
typedef enum {
    CONTROLLER_CONFIG_NOT_COMMITTED = 0,
    CONTROLLER_CONFIG_COMMITTED_VERIFIED,
    CONTROLLER_CONFIG_COMMITTED_UNVERIFIED,
} controller_config_write_result_t;

typedef struct {
    rk_cfg_t value;
    controller_config_durability_t durability;
    uint32_t revision;
} controller_config_snapshot_t;

typedef struct {
    rk_wifi_entry_t entries[RK_MAX_WIFI];
    uint8_t count;
    uint32_t revision;
} controller_config_wifi_snapshot_t;

/*
 * Runtime-only fencing for asynchronous endpoint work.  A token is copied
 * before mDNS or an HTTP request starts; a newer manual endpoint decision
 * makes the old token unusable without changing the persisted V3 blob.
 */
typedef struct {
    uint32_t generation;
    char bridge_base[sizeof(((rk_cfg_t *)0)->bridge_base)];
    bool from_mdns;
} controller_config_endpoint_token_t;

/*
 * Remote configuration is a narrow, presence-aware projection. It deliberately
 * contains no local connectivity, endpoint, zone, BLE, recovery, or UI fields.
 */
typedef enum {
    CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME = 1u << 0,
    CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA = 1u << 1,
    CONTROLLER_REMOTE_PREFERENCE_ROTATION_CHARGING = 1u << 2,
    CONTROLLER_REMOTE_PREFERENCE_ROTATION_NOT_CHARGING = 1u << 3,
    CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_ENABLED = 1u << 4,
    CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_TIMEOUT = 1u << 5,
    CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_ENABLED = 1u << 6,
    CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_TIMEOUT = 1u << 7,
    CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_ENABLED = 1u << 8,
    CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_TIMEOUT = 1u << 9,
    CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_ENABLED = 1u << 10,
    CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_TIMEOUT = 1u << 11,
    CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_ENABLED = 1u << 12,
    CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_TIMEOUT = 1u << 13,
    CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_ENABLED = 1u << 14,
    CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_TIMEOUT = 1u << 15,
    CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_ENABLED = 1u << 16,
    CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_TIMEOUT = 1u << 17,
    CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_ENABLED = 1u << 18,
    CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_TIMEOUT = 1u << 19,
    CONTROLLER_REMOTE_PREFERENCE_WIFI_POWER_SAVE_ENABLED = 1u << 20,
    CONTROLLER_REMOTE_PREFERENCE_CPU_FREQ_SCALING_ENABLED = 1u << 21,
    CONTROLLER_REMOTE_PREFERENCE_SLEEP_POLL_STOPPED = 1u << 22,
} controller_remote_preference_presence_t;

#define CONTROLLER_REMOTE_PREFERENCE_ALL                                      \
    (CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME |                                \
     CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA |                               \
     CONTROLLER_REMOTE_PREFERENCE_ROTATION_CHARGING |                        \
     CONTROLLER_REMOTE_PREFERENCE_ROTATION_NOT_CHARGING |                    \
     CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_ENABLED |                \
     CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_TIMEOUT |                \
     CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_ENABLED |                 \
     CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_TIMEOUT |                 \
     CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_ENABLED |                     \
     CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_TIMEOUT |                     \
     CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_ENABLED |                      \
     CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_TIMEOUT |                      \
     CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_ENABLED |                   \
     CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_TIMEOUT |                   \
     CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_ENABLED |                    \
     CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_TIMEOUT |                    \
     CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_ENABLED |              \
     CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_TIMEOUT |              \
     CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_ENABLED |               \
     CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_TIMEOUT |               \
     CONTROLLER_REMOTE_PREFERENCE_WIFI_POWER_SAVE_ENABLED |                  \
     CONTROLLER_REMOTE_PREFERENCE_CPU_FREQ_SCALING_ENABLED |                 \
     CONTROLLER_REMOTE_PREFERENCE_SLEEP_POLL_STOPPED)

typedef struct {
    uint32_t present;
    char knob_name[sizeof(((rk_cfg_t *)0)->knob_name)];
    char config_sha[sizeof(((rk_cfg_t *)0)->config_sha)];
    uint32_t rotation_charging;
    uint32_t rotation_not_charging;
    uint8_t art_mode_charging_enabled;
    uint32_t art_mode_charging_timeout_sec;
    uint8_t art_mode_battery_enabled;
    uint32_t art_mode_battery_timeout_sec;
    uint8_t dim_charging_enabled;
    uint32_t dim_charging_timeout_sec;
    uint8_t dim_battery_enabled;
    uint32_t dim_battery_timeout_sec;
    uint8_t sleep_charging_enabled;
    uint32_t sleep_charging_timeout_sec;
    uint8_t sleep_battery_enabled;
    uint32_t sleep_battery_timeout_sec;
    uint8_t deep_sleep_charging_enabled;
    uint32_t deep_sleep_charging_timeout_sec;
    uint8_t deep_sleep_battery_enabled;
    uint32_t deep_sleep_battery_timeout_sec;
    uint8_t wifi_power_save_enabled;
    uint8_t cpu_freq_scaling_enabled;
    uint32_t sleep_poll_stopped_sec;
} controller_remote_preferences_t;

/* Publish a copied startup snapshot for every durability outcome. */
bool controller_config_init(void);
bool controller_config_snapshot(controller_config_snapshot_t *out);
bool controller_config_wifi_snapshot(controller_config_wifi_snapshot_t *out);
bool controller_config_capture_endpoint_token(
    controller_config_endpoint_token_t *out);

/* Typed same-device mutations. A non-committed result leaves the snapshot intact. */
controller_config_write_result_t controller_config_upsert_wifi(
    const char *ssid, const char *pass, bool promote,
    controller_config_snapshot_t *out_committed);
controller_config_write_result_t controller_config_promote_wifi(
    size_t index, controller_config_snapshot_t *out_committed);
controller_config_write_result_t controller_config_remove_wifi(
    size_t index, controller_config_snapshot_t *out_committed);
controller_config_write_result_t controller_config_set_endpoint(
    const char *bridge_base, bool from_mdns,
    controller_config_snapshot_t *out_committed);
/*
 * Conditional endpoint replacement for asynchronous discovery.  When
 * require_current_empty is true, the current persisted endpoint must still be
 * empty as well as the token current.  A stale token is intentionally
 * indistinguishable from a non-committed write to callers.
 */
controller_config_write_result_t controller_config_set_endpoint_if_current(
    const controller_config_endpoint_token_t *token, const char *bridge_base,
    bool from_mdns, bool require_current_empty,
    controller_config_snapshot_t *out_committed);
controller_config_write_result_t controller_config_set_zone(
    const char *zone_id, controller_config_snapshot_t *out_committed);
controller_config_write_result_t controller_config_merge_remote_preferences(
    const controller_remote_preferences_t *preferences,
    controller_config_snapshot_t *out_committed);
controller_config_write_result_t
controller_config_merge_remote_preferences_if_endpoint_current(
    const controller_config_endpoint_token_t *token,
    const controller_remote_preferences_t *preferences,
    controller_config_snapshot_t *out_committed);

#if defined(CONTROLLER_CONFIG_TEST)
void controller_config_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif
