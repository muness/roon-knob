#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RK_CFG_CURRENT_VER 1

typedef struct {
    char ssid[33];
    char pass[65];
    char bridge_base[128];
    char zone_id[64];
    uint8_t cfg_ver;
} rk_cfg_t;

static inline bool rk_cfg_is_valid(const rk_cfg_t *cfg) {
    // Only check cfg_ver - bridge_base can be empty (mDNS auto-discovery)
    return cfg && cfg->cfg_ver != 0;
}

_Static_assert(sizeof(rk_cfg_t) == 291, "rk_cfg_t size changed");
