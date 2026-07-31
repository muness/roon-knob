#include "rk_ble_hid_host.h"

#include <assert.h>

void app_main(void) {
    rk_ble_hid_host_status_t status = {0};
    assert(rk_ble_hid_host_status_copy(&status) == RK_BLE_HID_HOST_OK);
    assert(status.state == RK_BLE_HID_HOST_STATE_UNAVAILABLE);
}
