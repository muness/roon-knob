#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Safe bounded string copy that avoids GCC -Wstringop-truncation and -Wrestrict
static inline void rk_strlcpy(char *dst, const char *src, size_t dst_size) {
    if (!dst_size) return;
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#define RK_CFG_CURRENT_VER 3
#define RK_CFG_V1_SIZE 291  // Size of v1 struct for migration
#define RK_CFG_V2_SIZE 374  // Size of v2 struct for migration
#define RK_MAX_WIFI 2

typedef struct {
    char ssid[33];
    char pass[65];
} rk_wifi_entry_t;

// Display config defaults (match bridge defaults)
#define RK_DEFAULT_ROTATION_CHARGING 180
#define RK_DEFAULT_ROTATION_NOT_CHARGING 0

// Art mode defaults (hide controls, full brightness)
#define RK_DEFAULT_ART_MODE_CHARGING_ENABLED 1
#define RK_DEFAULT_ART_MODE_CHARGING_TIMEOUT_SEC 60
#define RK_DEFAULT_ART_MODE_BATTERY_ENABLED 1
#define RK_DEFAULT_ART_MODE_BATTERY_TIMEOUT_SEC 30

// Dim defaults
#define RK_DEFAULT_DIM_CHARGING_ENABLED 1
#define RK_DEFAULT_DIM_CHARGING_TIMEOUT_SEC 120
#define RK_DEFAULT_DIM_BATTERY_ENABLED 1
#define RK_DEFAULT_DIM_BATTERY_TIMEOUT_SEC 30

// Sleep defaults (0 = never sleep while charging)
#define RK_DEFAULT_SLEEP_CHARGING_ENABLED 0
#define RK_DEFAULT_SLEEP_CHARGING_TIMEOUT_SEC 0
#define RK_DEFAULT_SLEEP_BATTERY_ENABLED 1
#define RK_DEFAULT_SLEEP_BATTERY_TIMEOUT_SEC 60

// Deep sleep defaults (after soft sleep, enter deep sleep for max power saving)
// Defaults to disabled when charging (device on USB power), but configurable
#define RK_DEFAULT_DEEP_SLEEP_CHARGING_ENABLED 0
#define RK_DEFAULT_DEEP_SLEEP_CHARGING_TIMEOUT_SEC 0
#define RK_DEFAULT_DEEP_SLEEP_BATTERY_ENABLED 1
#define RK_DEFAULT_DEEP_SLEEP_BATTERY_TIMEOUT_SEC 1200  // 20 minutes after soft sleep

// Power management defaults (disabled until proven stable)
#define RK_DEFAULT_WIFI_POWER_SAVE_ENABLED 0
#define RK_DEFAULT_CPU_FREQ_SCALING_ENABLED 0
#define RK_DEFAULT_SLEEP_POLL_STOPPED_SEC 60   // Poll interval when sleeping AND zone stopped

typedef struct {
    // === V1 fields (network config) - DO NOT REORDER ===
    char ssid[33];
    char pass[65];
    char bridge_base[128];
    char zone_id[64];
    uint8_t cfg_ver;  // V1 ended here (291 bytes)

    // === V2 fields (display config from bridge) ===
    char knob_name[32];
    char config_sha[9];  // 8 hex chars + null

    // Display rotation (0, 90, 180, 270)
    uint16_t rotation_charging;
    uint16_t rotation_not_charging;

    // Art mode settings (hide controls, keep full brightness)
    uint8_t art_mode_charging_enabled;
    uint16_t art_mode_charging_timeout_sec;
    uint8_t art_mode_battery_enabled;
    uint16_t art_mode_battery_timeout_sec;

    // Dim settings
    uint8_t dim_charging_enabled;
    uint16_t dim_charging_timeout_sec;
    uint8_t dim_battery_enabled;
    uint16_t dim_battery_timeout_sec;

    // Sleep settings
    uint8_t sleep_charging_enabled;
    uint16_t sleep_charging_timeout_sec;
    uint8_t sleep_battery_enabled;
    uint16_t sleep_battery_timeout_sec;

    // Deep sleep settings (after soft sleep, enter deep sleep for max power saving)
    uint8_t deep_sleep_charging_enabled;
    uint16_t deep_sleep_charging_timeout_sec;
    uint8_t deep_sleep_battery_enabled;
    uint16_t deep_sleep_battery_timeout_sec;

    // Power management (V2 addition)
    uint8_t wifi_power_save_enabled;     // Enable WiFi modem sleep when display sleeping
    uint8_t cpu_freq_scaling_enabled;    // Enable CPU frequency reduction when idle
    uint16_t sleep_poll_stopped_sec;     // Poll interval (sec) when sleeping AND zone stopped (0=use default 30s)

    // Bridge discovery source (V2 addition)
    uint8_t bridge_from_mdns;            // 1 if bridge was discovered via mDNS, 0 if manually configured

    // === V3 fields (multi-WiFi) ===
    rk_wifi_entry_t wifi[RK_MAX_WIFI];  // Known WiFi networks (tried in order)
    uint8_t wifi_count;                  // Number of valid entries in wifi[]
} rk_cfg_t;

static inline bool rk_cfg_is_valid(const rk_cfg_t *cfg) {
    // Only check cfg_ver - bridge_base can be empty (mDNS auto-discovery)
    return cfg && cfg->cfg_ver != 0;
}

// Initialize display config fields to defaults (for migration or new config)
static inline void rk_cfg_set_display_defaults(rk_cfg_t *cfg) {
    if (!cfg) return;
    cfg->knob_name[0] = '\0';
    cfg->config_sha[0] = '\0';
    cfg->rotation_charging = RK_DEFAULT_ROTATION_CHARGING;
    cfg->rotation_not_charging = RK_DEFAULT_ROTATION_NOT_CHARGING;
    cfg->art_mode_charging_enabled = RK_DEFAULT_ART_MODE_CHARGING_ENABLED;
    cfg->art_mode_charging_timeout_sec = RK_DEFAULT_ART_MODE_CHARGING_TIMEOUT_SEC;
    cfg->art_mode_battery_enabled = RK_DEFAULT_ART_MODE_BATTERY_ENABLED;
    cfg->art_mode_battery_timeout_sec = RK_DEFAULT_ART_MODE_BATTERY_TIMEOUT_SEC;
    cfg->dim_charging_enabled = RK_DEFAULT_DIM_CHARGING_ENABLED;
    cfg->dim_charging_timeout_sec = RK_DEFAULT_DIM_CHARGING_TIMEOUT_SEC;
    cfg->dim_battery_enabled = RK_DEFAULT_DIM_BATTERY_ENABLED;
    cfg->dim_battery_timeout_sec = RK_DEFAULT_DIM_BATTERY_TIMEOUT_SEC;
    cfg->sleep_charging_enabled = RK_DEFAULT_SLEEP_CHARGING_ENABLED;
    cfg->sleep_charging_timeout_sec = RK_DEFAULT_SLEEP_CHARGING_TIMEOUT_SEC;
    cfg->sleep_battery_enabled = RK_DEFAULT_SLEEP_BATTERY_ENABLED;
    cfg->sleep_battery_timeout_sec = RK_DEFAULT_SLEEP_BATTERY_TIMEOUT_SEC;
    cfg->deep_sleep_charging_enabled = RK_DEFAULT_DEEP_SLEEP_CHARGING_ENABLED;
    cfg->deep_sleep_charging_timeout_sec = RK_DEFAULT_DEEP_SLEEP_CHARGING_TIMEOUT_SEC;
    cfg->deep_sleep_battery_enabled = RK_DEFAULT_DEEP_SLEEP_BATTERY_ENABLED;
    cfg->deep_sleep_battery_timeout_sec = RK_DEFAULT_DEEP_SLEEP_BATTERY_TIMEOUT_SEC;
    cfg->wifi_power_save_enabled = RK_DEFAULT_WIFI_POWER_SAVE_ENABLED;
    cfg->cpu_freq_scaling_enabled = RK_DEFAULT_CPU_FREQ_SCALING_ENABLED;
    cfg->sleep_poll_stopped_sec = RK_DEFAULT_SLEEP_POLL_STOPPED_SEC;
    cfg->bridge_from_mdns = 0;  // Default until mDNS discovers
}

// Get effective rotation based on charging state
static inline uint16_t rk_cfg_get_rotation(const rk_cfg_t *cfg, bool is_charging) {
    if (!cfg) return 0;
    return is_charging ? cfg->rotation_charging : cfg->rotation_not_charging;
}

// Get effective art mode timeout based on charging state (0 = disabled)
static inline uint16_t rk_cfg_get_art_mode_timeout(const rk_cfg_t *cfg, bool is_charging) {
    if (!cfg) {
        return is_charging ? RK_DEFAULT_ART_MODE_CHARGING_TIMEOUT_SEC : RK_DEFAULT_ART_MODE_BATTERY_TIMEOUT_SEC;
    }
    if (is_charging) {
        return cfg->art_mode_charging_enabled ? cfg->art_mode_charging_timeout_sec : 0;
    }
    return cfg->art_mode_battery_enabled ? cfg->art_mode_battery_timeout_sec : 0;
}

// Get effective dim timeout based on charging state (0 = disabled)
static inline uint16_t rk_cfg_get_dim_timeout(const rk_cfg_t *cfg, bool is_charging) {
    if (!cfg) {
        return is_charging ? RK_DEFAULT_DIM_CHARGING_TIMEOUT_SEC : RK_DEFAULT_DIM_BATTERY_TIMEOUT_SEC;
    }
    if (is_charging) {
        return cfg->dim_charging_enabled ? cfg->dim_charging_timeout_sec : 0;
    }
    return cfg->dim_battery_enabled ? cfg->dim_battery_timeout_sec : 0;
}

// Get effective sleep timeout based on charging state (0 = disabled)
static inline uint16_t rk_cfg_get_sleep_timeout(const rk_cfg_t *cfg, bool is_charging) {
    if (!cfg) {
        return is_charging ? RK_DEFAULT_SLEEP_CHARGING_TIMEOUT_SEC : RK_DEFAULT_SLEEP_BATTERY_TIMEOUT_SEC;
    }
    if (is_charging) {
        return cfg->sleep_charging_enabled ? cfg->sleep_charging_timeout_sec : 0;
    }
    return cfg->sleep_battery_enabled ? cfg->sleep_battery_timeout_sec : 0;
}

// Get effective deep sleep timeout based on charging state (0 = disabled)
static inline uint16_t rk_cfg_get_deep_sleep_timeout(const rk_cfg_t *cfg, bool is_charging) {
    if (!cfg) {
        return is_charging ? RK_DEFAULT_DEEP_SLEEP_CHARGING_TIMEOUT_SEC : RK_DEFAULT_DEEP_SLEEP_BATTERY_TIMEOUT_SEC;
    }
    if (is_charging) {
        return cfg->deep_sleep_charging_enabled ? cfg->deep_sleep_charging_timeout_sec : 0;
    }
    return cfg->deep_sleep_battery_enabled ? cfg->deep_sleep_battery_timeout_sec : 0;
}

// Find a WiFi entry by SSID. Returns index or -1 if not found.
static inline int rk_cfg_find_wifi(const rk_cfg_t *cfg, const char *ssid) {
    if (!cfg || !ssid) return -1;
    for (int i = 0; i < cfg->wifi_count && i < RK_MAX_WIFI; i++) {
        if (strcmp(cfg->wifi[i].ssid, ssid) == 0) return i;
    }
    return -1;
}

// Add or update a WiFi entry. Returns the entry index, or -1 when full.
// Replacing a saved network must be an explicit user action.
static inline int rk_cfg_add_wifi(rk_cfg_t *cfg, const char *ssid, const char *pass) {
    if (!cfg || !ssid) return -1;
    int idx = rk_cfg_find_wifi(cfg, ssid);
    if (idx >= 0) {
        // Update existing entry
        if (pass) {
            rk_strlcpy(cfg->wifi[idx].pass, pass, sizeof(cfg->wifi[idx].pass));
        }
        return idx;
    }
    // Add new entry
    if (cfg->wifi_count < RK_MAX_WIFI) {
        idx = cfg->wifi_count++;
    } else {
        return -1;
    }
    rk_strlcpy(cfg->wifi[idx].ssid, ssid, sizeof(cfg->wifi[idx].ssid));
    cfg->wifi[idx].pass[0] = '\0';
    if (pass) {
        rk_strlcpy(cfg->wifi[idx].pass, pass, sizeof(cfg->wifi[idx].pass));
    }
    return idx;
}

// Remove a WiFi entry by index, shifting remaining entries down.
static inline void rk_cfg_remove_wifi(rk_cfg_t *cfg, int idx) {
    if (!cfg || cfg->wifi_count > RK_MAX_WIFI ||
        idx < 0 || idx >= cfg->wifi_count || idx >= RK_MAX_WIFI) {
        return;
    }
    for (int i = idx; i < cfg->wifi_count - 1 && i < RK_MAX_WIFI - 1; i++) {
        cfg->wifi[i] = cfg->wifi[i + 1];
    }
    cfg->wifi_count--;
    memset(&cfg->wifi[cfg->wifi_count], 0, sizeof(rk_wifi_entry_t));
}

// Keep legacy runtime credentials aligned with the first persisted entry.
// These fields remain in v3 for compatibility with existing controller code.
static inline void rk_cfg_sync_primary_wifi(rk_cfg_t *cfg) {
    if (!cfg) return;
    if (cfg->wifi_count > 0) {
        rk_strlcpy(cfg->ssid, cfg->wifi[0].ssid, sizeof(cfg->ssid));
        rk_strlcpy(cfg->pass, cfg->wifi[0].pass, sizeof(cfg->pass));
    } else {
        cfg->ssid[0] = '\0';
        cfg->pass[0] = '\0';
    }
}

// Move an existing entry to the front of the connection order.
static inline bool rk_cfg_promote_wifi(rk_cfg_t *cfg, int idx) {
    if (!cfg || cfg->wifi_count > RK_MAX_WIFI ||
        idx < 0 || idx >= cfg->wifi_count || idx >= RK_MAX_WIFI) {
        return false;
    }
    if (idx > 0) {
        rk_wifi_entry_t selected = cfg->wifi[idx];
        for (int i = idx; i > 0; i--) {
            cfg->wifi[i] = cfg->wifi[i - 1];
        }
        cfg->wifi[0] = selected;
    }
    rk_cfg_sync_primary_wifi(cfg);
    return true;
}

// Merge only this device's local connectivity into a controller-owned config.
// Bridge, zone, display, and power settings remain untouched.
static inline bool rk_cfg_copy_local_connectivity(rk_cfg_t *dst,
                                                  const rk_cfg_t *src) {
    if (!dst || !src || src->wifi_count > RK_MAX_WIFI) {
        return false;
    }
    memcpy(dst->wifi, src->wifi, sizeof(dst->wifi));
    dst->wifi_count = src->wifi_count;
    rk_strlcpy(dst->ssid, src->ssid, sizeof(dst->ssid));
    rk_strlcpy(dst->pass, src->pass, sizeof(dst->pass));
    dst->cfg_ver = RK_CFG_CURRENT_VER;
    return true;
}

// Repair a same-device v3 blob before any helper trusts its persisted count
// or strings. Returns true when the caller should persist the repaired blob.
static inline bool rk_cfg_normalize_wifi(rk_cfg_t *cfg) {
    if (!cfg) return false;
    bool changed = false;

    if (cfg->ssid[sizeof(cfg->ssid) - 1] != '\0') changed = true;
    if (cfg->pass[sizeof(cfg->pass) - 1] != '\0') changed = true;
    cfg->ssid[sizeof(cfg->ssid) - 1] = '\0';
    cfg->pass[sizeof(cfg->pass) - 1] = '\0';

    if (cfg->wifi_count > RK_MAX_WIFI) {
        memset(cfg->wifi, 0, sizeof(cfg->wifi));
        cfg->wifi_count = 0;
        if (cfg->ssid[0]) {
            (void)rk_cfg_add_wifi(cfg, cfg->ssid, cfg->pass);
        }
        changed = true;
    }

    for (int i = 0; i < cfg->wifi_count; i++) {
        if (cfg->wifi[i].ssid[sizeof(cfg->wifi[i].ssid) - 1] != '\0') {
            changed = true;
        }
        if (cfg->wifi[i].pass[sizeof(cfg->wifi[i].pass) - 1] != '\0') {
            changed = true;
        }
        cfg->wifi[i].ssid[sizeof(cfg->wifi[i].ssid) - 1] = '\0';
        cfg->wifi[i].pass[sizeof(cfg->wifi[i].pass) - 1] = '\0';
    }

    for (int i = 0; i < cfg->wifi_count;) {
        if (cfg->wifi[i].ssid[0] != '\0') {
            i++;
            continue;
        }
        rk_cfg_remove_wifi(cfg, i);
        changed = true;
    }

    char prior_ssid[sizeof(cfg->ssid)];
    char prior_pass[sizeof(cfg->pass)];
    rk_strlcpy(prior_ssid, cfg->ssid, sizeof(prior_ssid));
    rk_strlcpy(prior_pass, cfg->pass, sizeof(prior_pass));
    rk_cfg_sync_primary_wifi(cfg);
    if (strcmp(prior_ssid, cfg->ssid) != 0 ||
        strcmp(prior_pass, cfg->pass) != 0) {
        changed = true;
    }
    return changed;
}

/*
 * Make every persisted string safe before a helper reads it and normalize the
 * complete V3 compatibility DTO. This deliberately keeps the namespace, key,
 * field order, and field types unchanged; it only produces a deterministic
 * in-memory representation for a same-device candidate.
 */
static inline bool rk_cfg_terminate_string(char *value, size_t capacity) {
    if (!value || capacity == 0) {
        return false;
    }
    bool changed = value[capacity - 1] != '\0';
    value[capacity - 1] = '\0';
    return changed;
}

static inline bool rk_cfg_normalize_v3(rk_cfg_t *cfg) {
    if (!cfg) {
        return false;
    }

    bool changed = false;
    if (cfg->cfg_ver != RK_CFG_CURRENT_VER) {
        cfg->cfg_ver = RK_CFG_CURRENT_VER;
        changed = true;
    }

    changed |= rk_cfg_terminate_string(cfg->ssid, sizeof(cfg->ssid));
    changed |= rk_cfg_terminate_string(cfg->pass, sizeof(cfg->pass));
    changed |= rk_cfg_terminate_string(cfg->bridge_base,
                                        sizeof(cfg->bridge_base));
    changed |= rk_cfg_terminate_string(cfg->zone_id, sizeof(cfg->zone_id));
    changed |= rk_cfg_terminate_string(cfg->knob_name, sizeof(cfg->knob_name));
    changed |= rk_cfg_terminate_string(cfg->config_sha,
                                        sizeof(cfg->config_sha));
    for (size_t i = 0; i < RK_MAX_WIFI; ++i) {
        changed |= rk_cfg_terminate_string(cfg->wifi[i].ssid,
                                            sizeof(cfg->wifi[i].ssid));
        changed |= rk_cfg_terminate_string(cfg->wifi[i].pass,
                                            sizeof(cfg->wifi[i].pass));
    }

    if (rk_cfg_normalize_wifi(cfg)) {
        changed = true;
    }
    for (size_t i = cfg->wifi_count; i < RK_MAX_WIFI; ++i) {
        rk_wifi_entry_t empty = {0};
        if (memcmp(&cfg->wifi[i], &empty, sizeof(empty)) != 0) {
            cfg->wifi[i] = empty;
            changed = true;
        }
    }

    size_t bridge_len = strlen(cfg->bridge_base);
    while (bridge_len > 0 &&
           (cfg->bridge_base[bridge_len - 1] == '/' ||
            cfg->bridge_base[bridge_len - 1] == ' ' ||
            cfg->bridge_base[bridge_len - 1] == '\t' ||
            cfg->bridge_base[bridge_len - 1] == '\n' ||
            cfg->bridge_base[bridge_len - 1] == '\r')) {
        cfg->bridge_base[--bridge_len] = '\0';
        changed = true;
    }
    return changed;
}

/*
 * Decode a device-local raw compatibility blob into the current V3 candidate.
 * The caller owns the later persistence decision. Keeping this transformation
 * common makes Dial and Frame upgrades byte-for-byte compatible without
 * allowing a read operation to write NVS as a side effect.
 */
static inline void rk_cfg_set_storage_defaults(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    *cfg = (rk_cfg_t){0};
    rk_cfg_set_display_defaults(cfg);
    cfg->cfg_ver = RK_CFG_CURRENT_VER;
}

static inline bool rk_cfg_prepare_storage_read(rk_cfg_t *cfg,
                                               size_t stored_len) {
    if (!cfg) {
        return false;
    }

    bool needs_persist = false;
    if (stored_len == RK_CFG_V1_SIZE && cfg->cfg_ver == 1) {
        (void)rk_cfg_terminate_string(cfg->ssid, sizeof(cfg->ssid));
        (void)rk_cfg_terminate_string(cfg->pass, sizeof(cfg->pass));
        rk_cfg_set_display_defaults(cfg);
        if (cfg->ssid[0]) {
            (void)rk_cfg_add_wifi(cfg, cfg->ssid, cfg->pass);
        }
        cfg->cfg_ver = RK_CFG_CURRENT_VER;
        needs_persist = true;
    } else if (stored_len == RK_CFG_V2_SIZE && cfg->cfg_ver == 2) {
        (void)rk_cfg_terminate_string(cfg->ssid, sizeof(cfg->ssid));
        (void)rk_cfg_terminate_string(cfg->pass, sizeof(cfg->pass));
        if (cfg->ssid[0]) {
            (void)rk_cfg_add_wifi(cfg, cfg->ssid, cfg->pass);
        }
        cfg->cfg_ver = RK_CFG_CURRENT_VER;
        needs_persist = true;
    } else if (stored_len != sizeof(*cfg)) {
        rk_cfg_set_storage_defaults(cfg);
        needs_persist = true;
    }

    return rk_cfg_normalize_v3(cfg) || needs_persist;
}

/*
 * Copy every named V3 field into zero-filled storage. This makes comparison
 * independent of compiler padding and irrelevant bytes after string terminators
 * while preserving the exact persisted field layout.
 */
static inline bool rk_cfg_canonicalize_v3(rk_cfg_t *out, const rk_cfg_t *in) {
    if (!out || !in) {
        return false;
    }

    rk_cfg_t normalized = *in;
    (void)rk_cfg_normalize_v3(&normalized);
    *out = (rk_cfg_t){0};

    rk_strlcpy(out->ssid, normalized.ssid, sizeof(out->ssid));
    rk_strlcpy(out->pass, normalized.pass, sizeof(out->pass));
    rk_strlcpy(out->bridge_base, normalized.bridge_base,
               sizeof(out->bridge_base));
    rk_strlcpy(out->zone_id, normalized.zone_id, sizeof(out->zone_id));
    out->cfg_ver = normalized.cfg_ver;
    rk_strlcpy(out->knob_name, normalized.knob_name, sizeof(out->knob_name));
    rk_strlcpy(out->config_sha, normalized.config_sha, sizeof(out->config_sha));

    out->rotation_charging = normalized.rotation_charging;
    out->rotation_not_charging = normalized.rotation_not_charging;
    out->art_mode_charging_enabled = normalized.art_mode_charging_enabled;
    out->art_mode_charging_timeout_sec =
        normalized.art_mode_charging_timeout_sec;
    out->art_mode_battery_enabled = normalized.art_mode_battery_enabled;
    out->art_mode_battery_timeout_sec =
        normalized.art_mode_battery_timeout_sec;
    out->dim_charging_enabled = normalized.dim_charging_enabled;
    out->dim_charging_timeout_sec = normalized.dim_charging_timeout_sec;
    out->dim_battery_enabled = normalized.dim_battery_enabled;
    out->dim_battery_timeout_sec = normalized.dim_battery_timeout_sec;
    out->sleep_charging_enabled = normalized.sleep_charging_enabled;
    out->sleep_charging_timeout_sec = normalized.sleep_charging_timeout_sec;
    out->sleep_battery_enabled = normalized.sleep_battery_enabled;
    out->sleep_battery_timeout_sec = normalized.sleep_battery_timeout_sec;
    out->deep_sleep_charging_enabled = normalized.deep_sleep_charging_enabled;
    out->deep_sleep_charging_timeout_sec =
        normalized.deep_sleep_charging_timeout_sec;
    out->deep_sleep_battery_enabled = normalized.deep_sleep_battery_enabled;
    out->deep_sleep_battery_timeout_sec =
        normalized.deep_sleep_battery_timeout_sec;
    out->wifi_power_save_enabled = normalized.wifi_power_save_enabled;
    out->cpu_freq_scaling_enabled = normalized.cpu_freq_scaling_enabled;
    out->sleep_poll_stopped_sec = normalized.sleep_poll_stopped_sec;
    out->bridge_from_mdns = normalized.bridge_from_mdns;
    for (size_t i = 0; i < RK_MAX_WIFI; ++i) {
        rk_strlcpy(out->wifi[i].ssid, normalized.wifi[i].ssid,
                   sizeof(out->wifi[i].ssid));
        rk_strlcpy(out->wifi[i].pass, normalized.wifi[i].pass,
                   sizeof(out->wifi[i].pass));
    }
    out->wifi_count = normalized.wifi_count;
    return true;
}

static inline bool rk_cfg_v3_equal(const rk_cfg_t *left, const rk_cfg_t *right) {
    rk_cfg_t left_canonical;
    rk_cfg_t right_canonical;
    return rk_cfg_canonicalize_v3(&left_canonical, left) &&
           rk_cfg_canonicalize_v3(&right_canonical, right) &&
           memcmp(&left_canonical, &right_canonical,
                  sizeof(left_canonical)) == 0;
}

/* Compare an arbitrary candidate with an already-canonical V3 value using one
 * scratch record. Storage verification uses this after writing its canonical
 * candidate, avoiding the two-scratch general equality helper on ESP tasks. */
static inline bool rk_cfg_v3_equal_to_canonical(
    const rk_cfg_t *candidate, const rk_cfg_t *canonical) {
    if (!candidate || !canonical) {
        return false;
    }
    rk_cfg_t candidate_canonical;
    return rk_cfg_canonicalize_v3(&candidate_canonical, candidate) &&
           memcmp(&candidate_canonical, canonical,
                  sizeof(candidate_canonical)) == 0;
}
_Static_assert(sizeof(rk_cfg_t) == 570, "rk_cfg_t size changed - update migration sizes");
