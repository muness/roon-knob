#include "rk_ble_hid_policy.h"

#include <string.h>

bool rk_ble_hid_policy_resolve_enabled(bool stored_value_found,
                                       uint8_t stored_value,
                                       bool target_default) {
    return stored_value_found ? stored_value != 0 : target_default;
}

void *rk_ble_hid_policy_open_device(int open_status, void *borrowed_device) {
    return open_status == 0 ? borrowed_device : NULL;
}

bool rk_ble_hid_policy_snapshot_contains(
    const rk_ble_hid_host_device_t *device,
    const rk_ble_hid_host_device_t *snapshot, size_t snapshot_count,
    uint32_t current_generation) {
    if (!device || !snapshot ||
        device->scan_generation != current_generation) {
        return false;
    }
    for (size_t i = 0; i < snapshot_count; ++i) {
        if (snapshot[i].addr_type == device->addr_type &&
            memcmp(snapshot[i].bda, device->bda, sizeof(device->bda)) == 0) {
            return true;
        }
    }
    return false;
}

bool rk_ble_hid_policy_stop_ready(bool connect_worker_active,
                                  bool connect_callback_pending,
                                  bool have_devices) {
    return !connect_worker_active && !connect_callback_pending &&
           !have_devices;
}
