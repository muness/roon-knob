#pragma once

#include <stdbool.h>

// Initialize the shared BLE HID host with Frame's default-on policy and
// target-owned presentation adapter.
bool ble_hid_host_frame_start(void);
