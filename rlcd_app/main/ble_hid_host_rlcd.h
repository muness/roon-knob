#pragma once
#include <stdbool.h>
bool ble_hid_host_rlcd_start(void);

typedef enum {
    RLCD_BLE_SLEEP_FAILED = -1,
    RLCD_BLE_SLEEP_PENDING = 0,
    RLCD_BLE_SLEEP_READY = 1,
} rlcd_ble_sleep_status_t;

rlcd_ble_sleep_status_t ble_hid_host_rlcd_prepare_for_sleep(void);
bool ble_hid_host_rlcd_cancel_sleep(void);
