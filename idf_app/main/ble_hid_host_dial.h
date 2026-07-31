#pragma once

#include <stdbool.h>

// Initialize the shared BLE HID host with Dial's opt-in policy and target UI
// adapter. Safe to call once after the first STA connection.
bool ble_hid_host_dial_start(void);
