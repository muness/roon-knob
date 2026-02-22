#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Maximum BLE HID devices found during a scan
#define BLE_REMOTE_MAX_RESULTS 8

typedef struct {
    char name[64];
    uint8_t bda[6];       // Bluetooth Device Address
    uint8_t addr_type;
} ble_remote_device_t;

// Initialize BLE stack and HID host. Call once after NVS init.
// If a bonded device exists in NVS, auto-reconnect attempts start.
void ble_remote_init(void);

// Start an async scan for BLE HID devices (~5 seconds).
// Results available via ble_remote_get_scan_results() after scan completes.
void ble_remote_scan_start(void);

// True while a scan is in progress.
bool ble_remote_is_scanning(void);

// Copy scan results into out[]. Returns number of results (0..max).
int ble_remote_get_scan_results(ble_remote_device_t *out, int max);

// Pair with scan result at index. Bonds and saves BDA to NVS.
void ble_remote_pair(int index);

// Forget bonded device. Disconnects if connected.
void ble_remote_unpair(void);

// True if a BLE HID remote is currently connected.
bool ble_remote_is_connected(void);

// Copy name of connected or last-bonded device into out (empty string if none).
void ble_remote_device_name(char *out, size_t len);
