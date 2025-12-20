#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RK_CFG_CURRENT_VER 2
#define RK_CFG_V1_SIZE 291  // Size of v1 struct for migration

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

_Static_assert(sizeof(rk_cfg_t) == 360, "rk_cfg_t size changed - update RK_CFG_V1_SIZE if needed");
