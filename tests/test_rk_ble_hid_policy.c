#include "rk_ble_hid_policy.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(rk_ble_hid_policy_resolve_enabled(false, 0, true));
    assert(!rk_ble_hid_policy_resolve_enabled(false, 1, false));
    assert(rk_ble_hid_policy_resolve_enabled(true, 1, false));
    assert(!rk_ble_hid_policy_resolve_enabled(true, 0, true));

    int device_storage = 1;
    void *device = &device_storage;
    assert(rk_ble_hid_policy_open_device(0, device) == device);
    assert(rk_ble_hid_policy_open_device(-1, device) == NULL);

    const rk_ble_hid_host_device_t snapshot[] = {
        {
            .bda = {1, 2, 3, 4, 5, 6},
            .addr_type = 1,
            .scan_generation = 7,
        },
    };
    rk_ble_hid_host_device_t candidate = snapshot[0];
    assert(rk_ble_hid_policy_snapshot_contains(
        &candidate, snapshot, 1, 7));
    candidate.scan_generation = 6;
    assert(!rk_ble_hid_policy_snapshot_contains(
        &candidate, snapshot, 1, 7));
    candidate = snapshot[0];
    candidate.bda[5] = 9;
    assert(!rk_ble_hid_policy_snapshot_contains(
        &candidate, snapshot, 1, 7));

    assert(rk_ble_hid_policy_stop_ready(false, false, false));
    assert(!rk_ble_hid_policy_stop_ready(true, false, false));
    assert(!rk_ble_hid_policy_stop_ready(false, true, false));
    assert(!rk_ble_hid_policy_stop_ready(false, false, true));

    puts("rk_ble_hid_host lifecycle policy tests passed");
    return 0;
}
