#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "rk_cfg.h"

typedef enum {
    RK_NET_EVT_CONNECTING,
    RK_NET_EVT_GOT_IP,
    RK_NET_EVT_FAIL,
    RK_NET_EVT_AP_STARTED,
    RK_NET_EVT_AP_STOPPED,
    RK_NET_EVT_WRONG_PASSWORD,
    RK_NET_EVT_NO_AP_FOUND,
    RK_NET_EVT_AUTH_TIMEOUT,
} rk_net_evt_t;

void wifi_mgr_start(void);
void wifi_mgr_stop(void);
void wifi_mgr_reconnect(const rk_cfg_t *cfg);
void wifi_mgr_forget_wifi(void);
bool wifi_mgr_get_ip(char *buf, size_t n);
void wifi_mgr_get_ssid(char *buf, size_t n);
bool wifi_mgr_is_ap_mode(void);
const char *wifi_mgr_get_hostname(void);
void wifi_mgr_stop_ap(void);
const char *wifi_mgr_get_last_error(void);
int wifi_mgr_get_retry_count(void);
int wifi_mgr_get_retry_max(void);
void wifi_mgr_set_power_save(bool enable);

// weak callback the UI can override
void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt);
