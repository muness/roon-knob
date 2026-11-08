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
    case RK_BLE_HID_HOST_ERROR_SECURITY: return "SECURITY";
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

bool rk_ble_hid_host_decode_consumer_event(const uint8_t *data, size_t len,
                                           uint16_t *usage, bool *pressed) {
    if (!data || len < 2 || !usage || !pressed) {
        return false;
    }
    *usage = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    *pressed = *usage != 0;
    return true;
}

void rk_ble_consumer_tracker_reset(rk_ble_consumer_tracker_t *tracker) {
    if (tracker) {
        tracker->down = 0;
    }
}

bool rk_ble_consumer_tracker_holding(const rk_ble_consumer_tracker_t *tracker) {
    return tracker && tracker->down != 0;
}

size_t rk_ble_consumer_tracker_update(rk_ble_consumer_tracker_t *tracker,
                                      const uint8_t *data, size_t len,
                                      rk_ble_consumer_edge_t out[2]) {
    uint16_t usage = 0;
    bool pressed = false;
    if (!tracker || !out ||
        !rk_ble_hid_host_decode_consumer_event(data, len, &usage, &pressed)) {
        return 0;
    }
    size_t count = 0;
    if (!pressed) {
        if (tracker->down != 0) {
            out[count].usage = tracker->down;
            out[count].pressed = false;
            ++count;
            tracker->down = 0;
        }
        return count;
    }
    if (tracker->down == usage) {
        return 0; /* auto-repeat of a held key */
    }
    if (tracker->down != 0) {
        out[count].usage = tracker->down;
        out[count].pressed = false;
        ++count;
    }
    out[count].usage = usage;
    out[count].pressed = true;
    ++count;
    tracker->down = usage;
    return count;
}

bool rk_ble_hid_report_is_consumer_array(const uint8_t *map, size_t len,
                                        uint16_t report_id, size_t payload_len) {
    if (!map || payload_len < 2 || payload_len > RK_BLE_HID_HOST_REPORT_BYTES) return false;
    struct { uint32_t page, id, size, count, logical_min; } globals = {0}, stack[4];
    size_t depth = 0, bits = 0;
    uint32_t usage_min = 0;
    bool has_usage_min = false;
    for (size_t pos = 0; pos < len;) {
        const uint8_t prefix = map[pos++];
        const size_t width = (prefix & 3) == 3 ? 4 : (prefix & 3);
        if (prefix == 0xfe || width > len - pos) return false;
        uint32_t value = 0;
        for (size_t i = 0; i < width; ++i) value |= (uint32_t)map[pos++] << (8 * i);
        switch (prefix & 0xfc) {
        case 0x04: globals.page = value; break;  /* Usage Page */
        case 0x14: globals.logical_min = value; break; /* Logical Minimum */
        case 0x18: usage_min = value; has_usage_min = true; break; /* Usage Minimum */
        case 0x74: globals.size = value; break;  /* Report Size */
        case 0x84: globals.id = value; break;    /* Report ID */
        case 0x94: globals.count = value; break; /* Report Count */
        case 0xa4: /* Push */
            if (depth == sizeof(stack) / sizeof(stack[0])) return false;
            stack[depth++] = globals;
            break;
        case 0xb4: /* Pop */
            if (!depth) return false;
            globals = stack[--depth];
            break;
        case 0x80: /* Input */
            if (globals.id != report_id) break;
            if (!globals.size || !globals.count ||
                globals.count > (payload_len * 8 - bits) / globals.size) return false;
            if (!bits) {
                /* A Consumer selector whose values equal the usage IDs. */
                if ((value & 7) || globals.page != 0x0c ||
                    globals.size != 16 || globals.count != 1 ||
                    !has_usage_min || usage_min != globals.logical_min ||
                    usage_min > UINT16_MAX) return false;
            } else if (!(value & 1)) {
                /* Remaining bits are padding. */
                return false;
            }
            bits += globals.size * globals.count;
            break;
        }
        /* Local items apply only to the next Main item. */
        if ((prefix & 0x0c) == 0) has_usage_min = false;
    }
    return depth == 0 && bits == payload_len * 8;
}
