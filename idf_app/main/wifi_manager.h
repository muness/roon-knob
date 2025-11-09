#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "config_store.h"

typedef enum { RK_NET_EVT_CONNECTING, RK_NET_EVT_GOT_IP, RK_NET_EVT_FAIL } rk_net_evt_t;

void wifi_mgr_start(void);                   // call once at boot
void wifi_mgr_reconnect(const rk_cfg_t *cfg);   // apply new cfg and reconnect
void wifi_mgr_forget_wifi(void);             // clears ssid/pass, reconnects using defaults
bool wifi_mgr_get_ip(char *buf, size_t n);   // "a.b.c.d"
void wifi_mgr_get_ssid(char *buf, size_t n);

// weak callback the UI can override (or register separately)
void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt);
