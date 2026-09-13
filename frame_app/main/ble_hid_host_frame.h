#pragma once

#include <stdbool.h>

// Initialize the shared BLE HID host with Frame's default-on policy and
// target-owned presentation adapter.
bool ble_hid_host_frame_start(void);

typedef enum {
    FRAME_BLE_SLEEP_FAILED = -1,
    FRAME_BLE_SLEEP_PENDING = 0,
    FRAME_BLE_SLEEP_READY = 1,
} frame_ble_sleep_status_t;

frame_ble_sleep_status_t ble_hid_host_frame_prepare_for_sleep(void);
bool ble_hid_host_frame_cancel_sleep(void);
