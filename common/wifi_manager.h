#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "controller_config.h"

typedef enum {
    RK_NET_EVT_CONNECTING,   // Attempting STA connection
    RK_NET_EVT_GOT_IP,       // STA connected with IP
    RK_NET_EVT_FAIL,         // STA connection failed (will retry)
    RK_NET_EVT_AP_STARTED,   // Switched to AP mode for provisioning
    RK_NET_EVT_AP_STOPPED,   // AP mode stopped, switching to STA
    // Specific failure reasons (for better debugging)
    RK_NET_EVT_WRONG_PASSWORD,   // Auth failed - wrong password
    RK_NET_EVT_NO_AP_FOUND,      // Network not found
    RK_NET_EVT_AUTH_TIMEOUT,     // Authentication timeout
} rk_net_evt_t;

#define RK_WIFI_SCAN_MAX_NETWORKS 20

typedef struct {
    char ssid[33];
    int8_t rssi;
} rk_wifi_network_t;

void wifi_mgr_start(void);                   // call once at boot
void wifi_mgr_stop(void);                    // full stop (for BLE mode switch)
/* Apply a copied, already-committed local-connectivity projection. This never
 * persists. Set reconnect only for flows whose existing behavior reconnects. */
void wifi_mgr_apply_wifi(const controller_config_wifi_snapshot_t *wifi,
                         bool reconnect);
void wifi_mgr_forget_wifi(void);             // clears this device's configuration and reboots
bool wifi_mgr_get_ip(char *buf, size_t n);   // "a.b.c.d"
void wifi_mgr_get_ssid(char *buf, size_t n);
bool wifi_mgr_is_ap_mode(void);              // true if in AP provisioning mode
bool wifi_mgr_start_provisioning(void);       // true only when setup AP mode can start
const char *wifi_mgr_get_hostname(void);     // get device hostname (for mDNS, logs)
void wifi_mgr_stop_ap(void);                 // stop AP mode, attempt STA connection
const char *wifi_mgr_get_last_error(void);   // get last disconnect reason string
int wifi_mgr_get_retry_count(void);          // get current retry attempt count (0 = connected)
int wifi_mgr_get_retry_max(void);            // get max retries before AP mode
void wifi_mgr_set_power_save(bool enable);   // enable/disable WiFi modem sleep
/* Synchronously scan for visible 2.4 GHz networks. Safe while the setup AP
 * is running; callers provide storage for up to RK_WIFI_SCAN_MAX_NETWORKS. */
size_t wifi_mgr_scan_2g(rk_wifi_network_t *out, size_t capacity);

// weak callback the UI can override (or register separately)
void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt);
