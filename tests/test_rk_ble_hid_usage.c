#include "rk_ble_hid_host.h"

#include <assert.h>
#include <stdio.h>

static void expect(const uint8_t report[2], rk_ble_media_key_t expected) {
    rk_ble_media_key_t key = RK_BLE_MEDIA_KEY_NONE;
    assert(rk_ble_hid_host_map_consumer_report(report, 2, &key));
    assert(key == expected);
}

static void expect_three_byte_report(uint8_t usage,
                                     rk_ble_media_key_t expected) {
    const uint8_t report[] = {usage, 0x00, 0x00};
    rk_ble_media_key_t key = RK_BLE_MEDIA_KEY_NONE;
    assert(rk_ble_hid_host_map_consumer_report(report, sizeof(report), &key));
    assert(key == expected);
}

int main(void) {
    assert(rk_ble_hid_host_result_name(RK_BLE_HID_HOST_ERR_QUEUE_FULL)[0] == 'Q');
    assert(rk_ble_hid_host_error_name(RK_BLE_HID_HOST_ERROR_STALE_SCAN)[0] == 'S');
    expect((const uint8_t[]){0xcd, 0x00}, RK_BLE_MEDIA_KEY_PLAY_PAUSE);
    expect((const uint8_t[]){0xb5, 0x00}, RK_BLE_MEDIA_KEY_NEXT_TRACK);
    expect((const uint8_t[]){0xb6, 0x00}, RK_BLE_MEDIA_KEY_PREVIOUS_TRACK);
    expect((const uint8_t[]){0xe9, 0x00}, RK_BLE_MEDIA_KEY_VOLUME_UP);
    expect((const uint8_t[]){0xea, 0x00}, RK_BLE_MEDIA_KEY_VOLUME_DOWN);
    /* The tested remote emits these three-byte payloads while ESP-IDF 5.5
     * incorrectly labels their report metadata as GENERIC. */
    expect_three_byte_report(0xb5, RK_BLE_MEDIA_KEY_NEXT_TRACK);
    expect_three_byte_report(0xb6, RK_BLE_MEDIA_KEY_PREVIOUS_TRACK);
    expect_three_byte_report(0xe9, RK_BLE_MEDIA_KEY_VOLUME_UP);
    expect_three_byte_report(0xea, RK_BLE_MEDIA_KEY_VOLUME_DOWN);

    rk_ble_media_key_t key = RK_BLE_MEDIA_KEY_PLAY_PAUSE;
    assert(!rk_ble_hid_host_map_consumer_report((const uint8_t[]){0, 0}, 2, &key));
    assert(key == RK_BLE_MEDIA_KEY_NONE);
    assert(!rk_ble_hid_host_map_consumer_report((const uint8_t[]){0xe2, 0}, 2, &key));
    assert(!rk_ble_hid_host_map_consumer_report((const uint8_t[]){0xcd}, 1, &key));
    assert(!rk_ble_hid_host_map_consumer_report(NULL, 0, &key));
    puts("rk_ble_hid_host report mapping tests passed");
    return 0;
}
