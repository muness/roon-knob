#include "rk_ble_hid_host.h"

const char *rk_ble_hid_host_result_name(rk_ble_hid_host_result_t result) {
    switch (result) {
    case RK_BLE_HID_HOST_OK: return "OK";
    case RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case RK_BLE_HID_HOST_ERR_NOT_SUPPORTED: return "NOT_SUPPORTED";
    case RK_BLE_HID_HOST_ERR_ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
    case RK_BLE_HID_HOST_ERR_QUEUE_FULL: return "QUEUE_FULL";
    case RK_BLE_HID_HOST_ERR_INVALID_STATE: return "INVALID_STATE";
    default: return "UNKNOWN";
    }
}

const char *rk_ble_hid_host_error_name(rk_ble_hid_host_error_t error) {
    switch (error) {
    case RK_BLE_HID_HOST_ERROR_NONE: return "NONE";
    case RK_BLE_HID_HOST_ERROR_UNAVAILABLE: return "UNAVAILABLE";
    case RK_BLE_HID_HOST_ERROR_NVS: return "NVS";
    case RK_BLE_HID_HOST_ERROR_NIMBLE_INIT: return "NIMBLE_INIT";
    case RK_BLE_HID_HOST_ERROR_HID_INIT: return "HID_INIT";
    case RK_BLE_HID_HOST_ERROR_SYNC: return "SYNC";
    case RK_BLE_HID_HOST_ERROR_SCAN: return "SCAN";
    case RK_BLE_HID_HOST_ERROR_CONNECT: return "CONNECT";
    case RK_BLE_HID_HOST_ERROR_BOND_STORE: return "BOND_STORE";
    case RK_BLE_HID_HOST_ERROR_INVALID_STATE: return "INVALID_STATE";
    case RK_BLE_HID_HOST_ERROR_STALE_SCAN: return "STALE_SCAN";
    case RK_BLE_HID_HOST_ERROR_TEARDOWN: return "TEARDOWN";
    case RK_BLE_HID_HOST_ERROR_TEARDOWN_TIMEOUT: return "TEARDOWN_TIMEOUT";
    default: return "UNKNOWN";
    }
}

bool rk_ble_hid_host_map_consumer_report(const uint8_t *data, size_t len,
                                         rk_ble_media_key_t *key) {
    if (key) {
        *key = RK_BLE_MEDIA_KEY_NONE;
    }
    if (!data || len < 2 || !key) {
        return false;
    }

    const uint16_t usage = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    switch (usage) {
    case 0x00cd:
        *key = RK_BLE_MEDIA_KEY_PLAY_PAUSE;
        return true;
    case 0x00b5:
        *key = RK_BLE_MEDIA_KEY_NEXT_TRACK;
        return true;
    case 0x00b6:
        *key = RK_BLE_MEDIA_KEY_PREVIOUS_TRACK;
        return true;
    case 0x00e9:
        *key = RK_BLE_MEDIA_KEY_VOLUME_UP;
        return true;
    case 0x00ea:
        *key = RK_BLE_MEDIA_KEY_VOLUME_DOWN;
        return true;
    default:
        return false;
    }
}
