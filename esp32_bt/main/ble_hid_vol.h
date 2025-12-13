/**
 * @file ble_hid_vol.h
 * @brief BLE HID volume control (dual-mode with Classic BT AVRCP)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for BLE HID connection events
 * @param remote_bda Remote device Bluetooth address (6 bytes)
 */
typedef void (*ble_hid_connect_cb_t)(const uint8_t *remote_bda);

/**
 * @brief Initialize BLE HID for volume control
 *
 * Must be called after Bluedroid is initialized in dual-mode (BTDM).
 * Sets up BLE HID profile and starts advertising.
 *
 * @return true on success
 */
bool ble_hid_vol_init(void);

/**
 * @brief Deinitialize BLE HID
 */
void ble_hid_vol_deinit(void);

/**
 * @brief Set callback for BLE HID connection events
 *
 * The callback is invoked when a device connects via BLE HID,
 * allowing bt_avrcp to initiate an AVRCP connection to the same device.
 *
 * @param cb Callback function (receives remote device address)
 */
void ble_hid_vol_set_connect_callback(ble_hid_connect_cb_t cb);

/**
 * @brief Start directed BLE advertising to a specific device
 *
 * Call this after AVRCP connects to advertise specifically to that device.
 * Directed advertising is more likely to be noticed by the target device.
 *
 * @param peer_addr Remote device Bluetooth address (6 bytes)
 */
void ble_hid_vol_start_directed_advertising(const uint8_t *peer_addr);

/**
 * @brief Check if BLE HID is connected
 * @return true if a device is connected
 */
bool ble_hid_vol_is_connected(void);

/**
 * @brief Send volume up HID report
 */
void ble_hid_vol_up(void);

/**
 * @brief Send volume down HID report
 */
void ble_hid_vol_down(void);

/**
 * @brief Send play HID report
 */
void ble_hid_play(void);

/**
 * @brief Send pause HID report
 */
void ble_hid_pause(void);

/**
 * @brief Send next track HID report
 */
void ble_hid_next_track(void);

/**
 * @brief Send previous track HID report
 */
void ble_hid_prev_track(void);

#ifdef __cplusplus
}
#endif
