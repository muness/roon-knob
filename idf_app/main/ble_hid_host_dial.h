#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize the shared BLE HID host with Dial's opt-in policy and target UI
// adapter. Safe to call once after the first STA connection.
bool ble_hid_host_dial_start(void);

// Transiently quiesce the shared host before device Deep-sleep without
// changing its enabled preference or bond store. An uninitialized host is
// already safe. Returns false on an initialized-host error or timeout.
bool ble_hid_host_dial_prepare_for_sleep(uint32_t timeout_ms);
