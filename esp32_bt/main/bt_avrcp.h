/**
 * @file bt_avrcp.h
 * @brief Bluetooth AVRCP Controller interface
 *
 * Handles Classic Bluetooth connection and AVRCP profile for:
 * - Receiving track metadata from connected phone
 * - Sending media control commands (play, pause, next, prev, volume)
 */

#ifndef BT_AVRCP_H
#define BT_AVRCP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bluetooth connection state
 */
typedef enum {
    BT_STATE_DISCONNECTED = 0,
    BT_STATE_DISCOVERABLE,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
} bt_state_t;

/**
 * @brief Playback state
 */
typedef enum {
    PLAY_STATE_UNKNOWN = 0,
    PLAY_STATE_STOPPED,
    PLAY_STATE_PLAYING,
    PLAY_STATE_PAUSED,
} play_state_t;

/**
 * @brief Initialize Bluetooth and AVRCP Controller
 *
 * Sets up Classic Bluetooth stack, AVRCP controller profile,
 * and attempts to reconnect to last paired device.
 */
void bt_avrcp_init(void);

/**
 * @brief Deinitialize Bluetooth
 */
void bt_avrcp_deinit(void);

/**
 * @brief Get current Bluetooth state
 */
bt_state_t bt_avrcp_get_state(void);

/**
 * @brief Get current playback state
 */
play_state_t bt_avrcp_get_play_state(void);

/**
 * @brief Get connected device name
 * @return Device name or NULL if not connected
 */
const char *bt_avrcp_get_device_name(void);

/**
 * @brief Enter discoverable/pairing mode
 */
void bt_avrcp_enter_pairing_mode(void);

/**
 * @brief Disconnect from current device
 */
void bt_avrcp_disconnect(void);

/**
 * @brief Connect to last paired device
 */
void bt_avrcp_connect(void);

/**
 * @brief Send AVRCP play command
 */
void bt_avrcp_play(void);

/**
 * @brief Send AVRCP pause command
 */
void bt_avrcp_pause(void);

/**
 * @brief Toggle play/pause via BLE HID (for devices without AVRCP)
 */
void bt_avrcp_play_pause(void);

/**
 * @brief Send AVRCP next track command
 */
void bt_avrcp_next(void);

/**
 * @brief Send AVRCP previous track command
 */
void bt_avrcp_prev(void);

/**
 * @brief Send AVRCP volume up command
 */
void bt_avrcp_vol_up(void);

/**
 * @brief Send AVRCP volume down command
 */
void bt_avrcp_vol_down(void);

/**
 * @brief Set absolute volume via AVRCP
 * @param volume Volume level (0-127, AVRCP range)
 */
void bt_avrcp_set_volume(uint8_t volume);

/**
 * @brief Get current volume level
 * @return Volume level (0-127), or 0 if unknown
 */
uint8_t bt_avrcp_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_AVRCP_H */
