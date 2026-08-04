#include "rk_ble_hid_host.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    const rk_ble_hid_host_config_t config = {
        .default_enabled = true,
    };
    rk_ble_hid_host_status_t status = {0};
    rk_ble_hid_host_device_t device = {0};
    uint32_t generation = 99;

    assert(rk_ble_hid_host_init(&config) ==
           RK_BLE_HID_HOST_ERR_NOT_SUPPORTED);
    assert(rk_ble_hid_host_set_enabled(true) ==
           RK_BLE_HID_HOST_ERR_NOT_SUPPORTED);
    assert(rk_ble_hid_host_scan_start() ==
           RK_BLE_HID_HOST_ERR_NOT_SUPPORTED);
    assert(rk_ble_hid_host_scan_results_copy(&device, 1, &generation) == 0);
    assert(generation == 0);
    assert(rk_ble_hid_host_pair(&device) ==
           RK_BLE_HID_HOST_ERR_NOT_SUPPORTED);
    assert(rk_ble_hid_host_forget() ==
           RK_BLE_HID_HOST_ERR_NOT_SUPPORTED);
    assert(rk_ble_hid_host_status_copy(&status) == RK_BLE_HID_HOST_OK);
    assert(status.state == RK_BLE_HID_HOST_STATE_UNAVAILABLE);
    assert(status.last_error == RK_BLE_HID_HOST_ERROR_UNAVAILABLE);
    assert(rk_ble_hid_host_status_copy(NULL) ==
           RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT);

    puts("rk_ble_hid_host disabled-profile stub tests passed");
    return 0;
}
