#include "rk_ble_hid_host.h"

static rk_ble_hid_host_status_t s_status = {
    .state = RK_BLE_HID_HOST_STATE_UNAVAILABLE,
    .last_error = RK_BLE_HID_HOST_ERROR_UNAVAILABLE,
};

rk_ble_hid_host_result_t rk_ble_hid_host_init(const rk_ble_hid_host_config_t *config) {
    (void)config;
    return RK_BLE_HID_HOST_ERR_NOT_SUPPORTED;
}

rk_ble_hid_host_result_t rk_ble_hid_host_set_enabled(bool enabled) {
    (void)enabled;
    return RK_BLE_HID_HOST_ERR_NOT_SUPPORTED;
}

rk_ble_hid_host_result_t rk_ble_hid_host_scan_start(void) {
    return RK_BLE_HID_HOST_ERR_NOT_SUPPORTED;
}

size_t rk_ble_hid_host_scan_results_copy(rk_ble_hid_host_device_t *out, size_t capacity,
                                         uint32_t *scan_generation) {
    (void)out;
    (void)capacity;
    if (scan_generation) {
        *scan_generation = 0;
    }
    return 0;
}

rk_ble_hid_host_result_t rk_ble_hid_host_pair(const rk_ble_hid_host_device_t *device) {
    (void)device;
    return RK_BLE_HID_HOST_ERR_NOT_SUPPORTED;
}

rk_ble_hid_host_result_t rk_ble_hid_host_forget(void) {
    return RK_BLE_HID_HOST_ERR_NOT_SUPPORTED;
}

rk_ble_hid_host_result_t rk_ble_hid_host_status_copy(rk_ble_hid_host_status_t *out) {
    if (!out) {
        return RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT;
    }
    *out = s_status;
    return RK_BLE_HID_HOST_OK;
}
