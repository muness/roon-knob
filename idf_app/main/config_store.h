#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char ssid[33];
    char pass[65];
    char bridge_base[128];
    char zone_id[64];
    uint8_t cfg_ver; // start at 1
} rk_cfg_t;

bool rk_cfg_load(rk_cfg_t *out);            // returns false if empty
bool rk_cfg_save(const rk_cfg_t *in);
void rk_cfg_reset_wifi_only(void);          // clears ssid/pass only
