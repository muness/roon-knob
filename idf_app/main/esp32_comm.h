/**
 * @file esp32_comm.h
 * @brief UART communication with ESP32 (Classic Bluetooth chip)
 *
 * Binary TLV protocol for ESP32-S3 <-> ESP32 communication.
 * The ESP32 handles Classic Bluetooth (AVRCP/A2DP) and forwards
 * events to the S3 for display. The S3 sends commands to control
 * playback via AVRCP passthrough.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BT connection state from ESP32
 */
typedef enum {
    ESP32_BT_STATE_DISCONNECTED = 0,
    ESP32_BT_STATE_DISCOVERABLE = 1,
    ESP32_BT_STATE_CONNECTING = 2,
    ESP32_BT_STATE_CONNECTED = 3,
} esp32_bt_state_t;

/**
 * @brief Play status from ESP32
 */
typedef enum {
    ESP32_PLAY_STATE_UNKNOWN = 0,
    ESP32_PLAY_STATE_STOPPED = 1,
    ESP32_PLAY_STATE_PLAYING = 2,
    ESP32_PLAY_STATE_PAUSED = 3,
} esp32_play_state_t;

/**
 * @brief Metadata type
 */
typedef enum {
    ESP32_META_TITLE = 1,
    ESP32_META_ARTIST = 2,
    ESP32_META_ALBUM = 3,
    ESP32_META_DURATION = 4,
} esp32_meta_type_t;

/**
 * @brief Callback for BT state changes
 */
typedef void (*esp32_comm_bt_state_cb_t)(esp32_bt_state_t state);

/**
 * @brief Callback for play state changes
 */
typedef void (*esp32_comm_play_state_cb_t)(esp32_play_state_t state);

/**
 * @brief Callback for metadata updates
 */
typedef void (*esp32_comm_metadata_cb_t)(esp32_meta_type_t type, const char *text);

/**
 * @brief Callback for device name updates
 */
typedef void (*esp32_comm_device_name_cb_t)(const char *name);

/**
 * @brief Callback for connection health (pong received)
 */
typedef void (*esp32_comm_health_cb_t)(bool healthy);

/**
 * @brief Callback for volume changes
 * @param volume Volume level (0-127, AVRCP range)
 */
typedef void (*esp32_comm_volume_cb_t)(uint8_t volume);

/**
 * @brief Callback for play position changes
 * @param position_ms Current position in milliseconds
 */
typedef void (*esp32_comm_position_cb_t)(uint32_t position_ms);

/**
 * @brief Initialize UART communication with ESP32
 *
 * Sets up UART1 on GPIO17 (TX) and GPIO18 (RX) at 1Mbps.
 * Starts RX task and heartbeat timer.
 */
void esp32_comm_init(void);

/**
 * @brief Deinitialize UART communication
 */
void esp32_comm_deinit(void);

/**
 * @brief Check if ESP32 communication is healthy
 * @return true if pong received within timeout
 */
bool esp32_comm_is_healthy(void);

/**
 * @brief Get current BT state
 */
esp32_bt_state_t esp32_comm_get_bt_state(void);

/**
 * @brief Get current play state
 */
esp32_play_state_t esp32_comm_get_play_state(void);

/**
 * @brief Get current track title
 * @return Pointer to static buffer, or empty string if not set
 */
const char *esp32_comm_get_title(void);

/**
 * @brief Get current track artist
 * @return Pointer to static buffer, or empty string if not set
 */
const char *esp32_comm_get_artist(void);

/**
 * @brief Get current track album
 * @return Pointer to static buffer, or empty string if not set
 */
const char *esp32_comm_get_album(void);

/**
 * @brief Get connected device name
 * @return Pointer to static buffer, or empty string if not connected
 */
const char *esp32_comm_get_device_name(void);

/**
 * @brief Get current volume level
 * @return Volume level (0-127, AVRCP range)
 */
uint8_t esp32_comm_get_volume(void);

/**
 * @brief Get current track duration
 * @return Duration in milliseconds, or 0 if unknown
 */
uint32_t esp32_comm_get_duration(void);

/**
 * @brief Get current play position
 * @return Position in milliseconds
 */
uint32_t esp32_comm_get_position(void);

/* Command functions - send commands to ESP32 */

void esp32_comm_send_play(void);
void esp32_comm_send_pause(void);
void esp32_comm_send_play_pause(void);  // Toggle (for HID-only mode)
void esp32_comm_send_next(void);
void esp32_comm_send_prev(void);
void esp32_comm_send_vol_up(void);
void esp32_comm_send_vol_down(void);
void esp32_comm_send_set_volume(uint8_t volume);
void esp32_comm_send_bt_connect(void);
void esp32_comm_send_bt_disconnect(void);
void esp32_comm_send_bt_pair_mode(void);
void esp32_comm_send_bt_activate(void);    // Activate BT on ESP32
void esp32_comm_send_bt_deactivate(void);  // Deactivate BT on ESP32

/* Callback registration */

void esp32_comm_set_bt_state_cb(esp32_comm_bt_state_cb_t cb);
void esp32_comm_set_play_state_cb(esp32_comm_play_state_cb_t cb);
void esp32_comm_set_metadata_cb(esp32_comm_metadata_cb_t cb);
void esp32_comm_set_device_name_cb(esp32_comm_device_name_cb_t cb);
void esp32_comm_set_health_cb(esp32_comm_health_cb_t cb);
void esp32_comm_set_volume_cb(esp32_comm_volume_cb_t cb);
void esp32_comm_set_position_cb(esp32_comm_position_cb_t cb);

#ifdef __cplusplus
}
#endif
