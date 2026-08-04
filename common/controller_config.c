#include "controller_config.h"

#include "os_mutex.h"
#include "platform/platform_storage.h"

#include <limits.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifndef CONFIG_RK_DEFAULT_SSID
#define CONFIG_RK_DEFAULT_SSID ""
#endif

#ifndef CONFIG_RK_DEFAULT_PASS
#define CONFIG_RK_DEFAULT_PASS ""
#endif

typedef struct {
    os_mutex_t lock;
    bool initialized;
    controller_config_snapshot_t snapshot;
    uint32_t endpoint_generation;
} controller_config_state_t;

static controller_config_state_t s_config = {
    .lock = OS_MUTEX_INITIALIZER,
};

static bool lock_config(void) {
    return os_mutex_lock(&s_config.lock) == 0;
}

static void unlock_config(void) {
    (void)os_mutex_unlock(&s_config.lock);
}

static bool text_is_terminated(const char *value, size_t capacity,
                               bool allow_empty) {
    if (!value || capacity == 0 || (!allow_empty && value[0] == '\0')) {
        return false;
    }
    for (size_t i = 0; i < capacity; ++i) {
        if (value[i] == '\0') {
            return true;
        }
    }
    return false;
}

static void copy_checked_text(char *dst, size_t capacity, const char *src) {
    size_t length = 0;
    while (length + 1 < capacity && src[length] != '\0') {
        ++length;
    }
    memset(dst, 0, capacity);
    memcpy(dst, src, length);
}

static bool apply_compiled_wifi_defaults(rk_cfg_t *cfg) {
    if (!cfg || cfg->wifi_count != 0 || CONFIG_RK_DEFAULT_SSID[0] == '\0') {
        return false;
    }
    if (!text_is_terminated(CONFIG_RK_DEFAULT_SSID, sizeof(cfg->ssid), false) ||
        !text_is_terminated(CONFIG_RK_DEFAULT_PASS, sizeof(cfg->pass), true)) {
        return false;
    }
    if (rk_cfg_add_wifi(cfg, CONFIG_RK_DEFAULT_SSID,
                        CONFIG_RK_DEFAULT_PASS) < 0) {
        return false;
    }
    rk_cfg_sync_primary_wifi(cfg);
    return true;
}

static void advance_revision_locked(void) {
    if (s_config.snapshot.revision < UINT32_MAX) {
        ++s_config.snapshot.revision;
    }
}

static void advance_endpoint_generation_locked(void) {
    ++s_config.endpoint_generation;
    /* Zero is reserved for the initial generation.  Wrapping after billions
     * of endpoint decisions is safer than saturating, which would make every
     * later token permanently indistinguishable from a stale one. */
    if (s_config.endpoint_generation == 0) {
        ++s_config.endpoint_generation;
    }
}

static void publish_locked(const rk_cfg_t *candidate,
                           controller_config_durability_t durability) {
    rk_cfg_t canonical;
    if (!rk_cfg_canonicalize_v3(&canonical, candidate)) {
        return;
    }
    s_config.snapshot.value = canonical;
    s_config.snapshot.durability = durability;
    advance_revision_locked();
}

static controller_config_write_result_t public_write_result(
    platform_storage_write_result_t result) {
    switch (result) {
    case PLATFORM_STORAGE_COMMITTED_VERIFIED:
        return CONTROLLER_CONFIG_COMMITTED_VERIFIED;
    case PLATFORM_STORAGE_COMMITTED_UNVERIFIED:
        return CONTROLLER_CONFIG_COMMITTED_UNVERIFIED;
    case PLATFORM_STORAGE_NOT_COMMITTED:
    default:
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
}

static controller_config_write_result_t commit_candidate_locked(
    const rk_cfg_t *candidate, controller_config_snapshot_t *out_committed) {
    rk_cfg_t canonical;
    if (!candidate || !rk_cfg_canonicalize_v3(&canonical, candidate)) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    platform_storage_write_result_t result = platform_storage_write(&canonical);
    if (result == PLATFORM_STORAGE_NOT_COMMITTED) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    publish_locked(&canonical,
                   result == PLATFORM_STORAGE_COMMITTED_VERIFIED
                       ? CONTROLLER_CONFIG_DURABILITY_DURABLE
                       : CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT);
    if (out_committed) {
        *out_committed = s_config.snapshot;
    }
    return public_write_result(result);
}

bool controller_config_init(void) {
    if (!lock_config()) {
        return false;
    }
    if (s_config.initialized) {
        unlock_config();
        return true;
    }

    rk_cfg_t candidate = {0};
    platform_storage_read_result_t read_result;
    bool read_ok = platform_storage_read(&candidate, &read_result);
    bool needs_persist =
        read_ok &&
        (read_result.needs_persist ||
         read_result.state == PLATFORM_STORAGE_READ_RECOVERED);
    controller_config_durability_t durability =
        CONTROLLER_CONFIG_DURABILITY_DURABLE;

    if (!read_ok || read_result.state == PLATFORM_STORAGE_READ_ERROR) {
        platform_storage_defaults(&candidate);
        apply_compiled_wifi_defaults(&candidate);
        durability = CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY;
    } else {
        if (read_result.state == PLATFORM_STORAGE_READ_EMPTY) {
            platform_storage_defaults(&candidate);
            needs_persist = true;
        }

        /* Preserve the previous Wi-Fi manager policy: compiled credentials
         * fill any otherwise-empty same-device configuration, not only brand
         * new NVS. */
        if (apply_compiled_wifi_defaults(&candidate)) {
            needs_persist = true;
        }

        if (needs_persist) {
            platform_storage_write_result_t result =
                platform_storage_write(&candidate);
            if (result == PLATFORM_STORAGE_COMMITTED_UNVERIFIED) {
                durability = CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT;
            } else if (result == PLATFORM_STORAGE_NOT_COMMITTED) {
                /* A decoded same-device configuration remains the best
                 * recovery value even when its normalization/migration could
                 * not be persisted.  Publish it for this boot and surface the
                 * volatile durability warning; never replace readable user
                 * settings with defaults merely because the repair write
                 * failed. */
                durability = CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY;
            }
        }
    }

    publish_locked(&candidate, durability);
    s_config.initialized = true;
    unlock_config();
    return true;
}

bool controller_config_snapshot(controller_config_snapshot_t *out) {
    if (!out || !lock_config()) {
        return false;
    }
    bool available = s_config.initialized;
    if (available) {
        *out = s_config.snapshot;
    }
    unlock_config();
    return available;
}

bool controller_config_wifi_snapshot(controller_config_wifi_snapshot_t *out) {
    if (!out || !lock_config()) {
        return false;
    }
    bool available = s_config.initialized;
    if (available) {
        memcpy(out->entries, s_config.snapshot.value.wifi,
               sizeof(out->entries));
        out->count = s_config.snapshot.value.wifi_count;
        out->revision = s_config.snapshot.revision;
    }
    unlock_config();
    return available;
}

bool controller_config_capture_endpoint_token(
    controller_config_endpoint_token_t *out) {
    if (!out || !lock_config()) {
        return false;
    }
    bool available = s_config.initialized;
    if (available) {
        out->generation = s_config.endpoint_generation;
        copy_checked_text(out->bridge_base, sizeof(out->bridge_base),
                          s_config.snapshot.value.bridge_base);
        out->from_mdns = s_config.snapshot.value.bridge_from_mdns != 0;
    }
    unlock_config();
    return available;
}

controller_config_write_result_t controller_config_upsert_wifi(
    const char *ssid, const char *pass, bool promote,
    controller_config_snapshot_t *out_committed) {
    if (!text_is_terminated(ssid, sizeof(((rk_wifi_entry_t *)0)->ssid), false) ||
        !text_is_terminated(pass, sizeof(((rk_wifi_entry_t *)0)->pass), true) ||
        !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    int index = rk_cfg_add_wifi(&candidate, ssid, pass);
    if (index < 0 || (promote && !rk_cfg_promote_wifi(&candidate, index))) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    rk_cfg_sync_primary_wifi(&candidate);
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    unlock_config();
    return result;
}

controller_config_write_result_t controller_config_promote_wifi(
    size_t index, controller_config_snapshot_t *out_committed) {
    if (index >= RK_MAX_WIFI || !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    if (!rk_cfg_promote_wifi(&candidate, (int)index)) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    unlock_config();
    return result;
}

controller_config_write_result_t controller_config_remove_wifi(
    size_t index, controller_config_snapshot_t *out_committed) {
    if (index >= RK_MAX_WIFI || !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized || index >= s_config.snapshot.value.wifi_count) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    rk_cfg_remove_wifi(&candidate, (int)index);
    rk_cfg_sync_primary_wifi(&candidate);
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    unlock_config();
    return result;
}

controller_config_write_result_t controller_config_set_endpoint(
    const char *bridge_base, bool from_mdns,
    controller_config_snapshot_t *out_committed) {
    if (!text_is_terminated(bridge_base,
                            sizeof(((rk_cfg_t *)0)->bridge_base), true) ||
        !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    copy_checked_text(candidate.bridge_base, sizeof(candidate.bridge_base),
                      bridge_base);
    candidate.bridge_from_mdns = from_mdns ? 1 : 0;
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    if (result != CONTROLLER_CONFIG_NOT_COMMITTED) {
        advance_endpoint_generation_locked();
    }
    unlock_config();
    return result;
}

controller_config_write_result_t controller_config_set_endpoint_if_current(
    const controller_config_endpoint_token_t *token, const char *bridge_base,
    bool from_mdns, bool require_current_empty,
    controller_config_snapshot_t *out_committed) {
    if (!token ||
        !text_is_terminated(bridge_base,
                            sizeof(((rk_cfg_t *)0)->bridge_base), true) ||
        !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized ||
        token->generation != s_config.endpoint_generation ||
        (require_current_empty &&
         s_config.snapshot.value.bridge_base[0] != '\0')) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    copy_checked_text(candidate.bridge_base, sizeof(candidate.bridge_base),
                      bridge_base);
    candidate.bridge_from_mdns = from_mdns ? 1 : 0;
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    if (result != CONTROLLER_CONFIG_NOT_COMMITTED) {
        advance_endpoint_generation_locked();
    }
    unlock_config();
    return result;
}

controller_config_write_result_t controller_config_set_zone(
    const char *zone_id, controller_config_snapshot_t *out_committed) {
    if (!text_is_terminated(zone_id, sizeof(((rk_cfg_t *)0)->zone_id), true) ||
        !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    copy_checked_text(candidate.zone_id, sizeof(candidate.zone_id), zone_id);
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    unlock_config();
    return result;
}

static bool remote_preferences_are_valid(
    const controller_remote_preferences_t *preferences) {
    if (!preferences || preferences->present == 0 ||
        (preferences->present & ~CONTROLLER_REMOTE_PREFERENCE_ALL) != 0) {
        return false;
    }

    if ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME) != 0 &&
        !text_is_terminated(preferences->knob_name,
                            sizeof(preferences->knob_name), true)) {
        return false;
    }
    if ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA) != 0 &&
        !text_is_terminated(preferences->config_sha,
                            sizeof(preferences->config_sha), true)) {
        return false;
    }

    if (((preferences->present & CONTROLLER_REMOTE_PREFERENCE_ROTATION_CHARGING) !=
             0 &&
         preferences->rotation_charging != 0 &&
         preferences->rotation_charging != 90 &&
         preferences->rotation_charging != 180 &&
         preferences->rotation_charging != 270) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_ROTATION_NOT_CHARGING) != 0 &&
         preferences->rotation_not_charging != 0 &&
         preferences->rotation_not_charging != 90 &&
         preferences->rotation_not_charging != 180 &&
         preferences->rotation_not_charging != 270)) {
        return false;
    }

    if (((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_ENABLED) != 0 &&
         preferences->art_mode_charging_enabled > 1) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_ENABLED) != 0 &&
         preferences->art_mode_battery_enabled > 1) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_ENABLED) !=
             0 &&
         preferences->dim_charging_enabled > 1) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_ENABLED) !=
             0 &&
         preferences->dim_battery_enabled > 1) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_ENABLED) !=
             0 &&
         preferences->sleep_charging_enabled > 1) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_ENABLED) !=
             0 &&
         preferences->sleep_battery_enabled > 1) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_ENABLED) != 0 &&
         preferences->deep_sleep_charging_enabled > 1) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_ENABLED) != 0 &&
         preferences->deep_sleep_battery_enabled > 1) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_WIFI_POWER_SAVE_ENABLED) != 0 &&
         preferences->wifi_power_save_enabled > 1) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_CPU_FREQ_SCALING_ENABLED) != 0 &&
         preferences->cpu_freq_scaling_enabled > 1)) {
        return false;
    }

    if (((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_TIMEOUT) != 0 &&
         preferences->art_mode_charging_timeout_sec > UINT16_MAX) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_TIMEOUT) != 0 &&
         preferences->art_mode_battery_timeout_sec > UINT16_MAX) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_TIMEOUT) !=
             0 &&
         preferences->dim_charging_timeout_sec > UINT16_MAX) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_TIMEOUT) !=
             0 &&
         preferences->dim_battery_timeout_sec > UINT16_MAX) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_TIMEOUT) !=
             0 &&
         preferences->sleep_charging_timeout_sec > UINT16_MAX) ||
        ((preferences->present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_TIMEOUT) !=
             0 &&
         preferences->sleep_battery_timeout_sec > UINT16_MAX) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_TIMEOUT) != 0 &&
         preferences->deep_sleep_charging_timeout_sec > UINT16_MAX) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_TIMEOUT) != 0 &&
         preferences->deep_sleep_battery_timeout_sec > UINT16_MAX) ||
        ((preferences->present &
          CONTROLLER_REMOTE_PREFERENCE_SLEEP_POLL_STOPPED) != 0 &&
         preferences->sleep_poll_stopped_sec > UINT16_MAX)) {
        return false;
    }
    return true;
}

static void apply_remote_preferences(
    rk_cfg_t *candidate, const controller_remote_preferences_t *preferences) {
    uint32_t present = preferences->present;
    if ((present & CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME) != 0) {
        copy_checked_text(candidate->knob_name, sizeof(candidate->knob_name),
                          preferences->knob_name);
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA) != 0) {
        copy_checked_text(candidate->config_sha, sizeof(candidate->config_sha),
                          preferences->config_sha);
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_ROTATION_CHARGING) != 0) {
        candidate->rotation_charging = (uint16_t)preferences->rotation_charging;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_ROTATION_NOT_CHARGING) != 0) {
        candidate->rotation_not_charging =
            (uint16_t)preferences->rotation_not_charging;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_ENABLED) != 0) {
        candidate->art_mode_charging_enabled =
            preferences->art_mode_charging_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_ART_MODE_CHARGING_TIMEOUT) != 0) {
        candidate->art_mode_charging_timeout_sec =
            (uint16_t)preferences->art_mode_charging_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_ENABLED) != 0) {
        candidate->art_mode_battery_enabled =
            preferences->art_mode_battery_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_ART_MODE_BATTERY_TIMEOUT) != 0) {
        candidate->art_mode_battery_timeout_sec =
            (uint16_t)preferences->art_mode_battery_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_ENABLED) != 0) {
        candidate->dim_charging_enabled = preferences->dim_charging_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DIM_CHARGING_TIMEOUT) != 0) {
        candidate->dim_charging_timeout_sec =
            (uint16_t)preferences->dim_charging_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_ENABLED) != 0) {
        candidate->dim_battery_enabled = preferences->dim_battery_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DIM_BATTERY_TIMEOUT) != 0) {
        candidate->dim_battery_timeout_sec =
            (uint16_t)preferences->dim_battery_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_ENABLED) != 0) {
        candidate->sleep_charging_enabled = preferences->sleep_charging_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_CHARGING_TIMEOUT) != 0) {
        candidate->sleep_charging_timeout_sec =
            (uint16_t)preferences->sleep_charging_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_ENABLED) != 0) {
        candidate->sleep_battery_enabled = preferences->sleep_battery_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_BATTERY_TIMEOUT) != 0) {
        candidate->sleep_battery_timeout_sec =
            (uint16_t)preferences->sleep_battery_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_ENABLED) !=
        0) {
        candidate->deep_sleep_charging_enabled =
            preferences->deep_sleep_charging_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_CHARGING_TIMEOUT) !=
        0) {
        candidate->deep_sleep_charging_timeout_sec =
            (uint16_t)preferences->deep_sleep_charging_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_ENABLED) !=
        0) {
        candidate->deep_sleep_battery_enabled =
            preferences->deep_sleep_battery_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_DEEP_SLEEP_BATTERY_TIMEOUT) !=
        0) {
        candidate->deep_sleep_battery_timeout_sec =
            (uint16_t)preferences->deep_sleep_battery_timeout_sec;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_WIFI_POWER_SAVE_ENABLED) != 0) {
        candidate->wifi_power_save_enabled = preferences->wifi_power_save_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_CPU_FREQ_SCALING_ENABLED) != 0) {
        candidate->cpu_freq_scaling_enabled = preferences->cpu_freq_scaling_enabled;
    }
    if ((present & CONTROLLER_REMOTE_PREFERENCE_SLEEP_POLL_STOPPED) != 0) {
        candidate->sleep_poll_stopped_sec =
            (uint16_t)preferences->sleep_poll_stopped_sec;
    }
}

controller_config_write_result_t controller_config_merge_remote_preferences(
    const controller_remote_preferences_t *preferences,
    controller_config_snapshot_t *out_committed) {
    if (!remote_preferences_are_valid(preferences) || !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    apply_remote_preferences(&candidate, preferences);
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    unlock_config();
    return result;
}

controller_config_write_result_t
controller_config_merge_remote_preferences_if_endpoint_current(
    const controller_config_endpoint_token_t *token,
    const controller_remote_preferences_t *preferences,
    controller_config_snapshot_t *out_committed) {
    if (!token || !remote_preferences_are_valid(preferences) || !lock_config()) {
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }
    if (!s_config.initialized ||
        token->generation != s_config.endpoint_generation) {
        unlock_config();
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    }

    rk_cfg_t candidate = s_config.snapshot.value;
    apply_remote_preferences(&candidate, preferences);
    controller_config_write_result_t result =
        commit_candidate_locked(&candidate, out_committed);
    unlock_config();
    return result;
}

#if defined(CONTROLLER_CONFIG_TEST)
void controller_config_reset_for_test(void) {
    if (!lock_config()) {
        return;
    }
    s_config.initialized = false;
    s_config.snapshot = (controller_config_snapshot_t){0};
    s_config.endpoint_generation = 0;
    unlock_config();
}
#endif
