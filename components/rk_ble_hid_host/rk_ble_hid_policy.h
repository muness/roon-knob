#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rk_ble_hid_host.h"

bool rk_ble_hid_policy_resolve_enabled(bool stored_value_found,
                                       uint8_t stored_value,
                                       bool target_default);

void *rk_ble_hid_policy_open_device(int open_status, void *borrowed_device);

bool rk_ble_hid_policy_snapshot_contains(
    const rk_ble_hid_host_device_t *device,
    const rk_ble_hid_host_device_t *snapshot, size_t snapshot_count,
    uint32_t current_generation);

bool rk_ble_hid_policy_stop_ready(bool connect_worker_active,
                                  bool connect_callback_pending,
                                  bool have_devices);
