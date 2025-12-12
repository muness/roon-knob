/**
 * @file uart_protocol.h
 * @brief UART protocol for ESP32 <-> ESP32-S3 communication
 *
 * Binary TLV protocol:
 * | Start | Type | Length | Payload | CRC8 | End |
 * | 0x7E  | 1B   | 2B LE  | N bytes | 1B   | 0x7F|
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Frame delimiters
#define FRAME_START     0x7E
#define FRAME_END       0x7F

// Commands (S3 -> ESP32)
#define CMD_PLAY        0x01
#define CMD_PAUSE       0x02
#define CMD_NEXT        0x03
#define CMD_PREV        0x04
#define CMD_VOL_UP      0x05
#define CMD_VOL_DOWN    0x06
#define CMD_SET_VOLUME  0x07    // Set absolute volume (0-127)
#define CMD_BT_CONNECT  0x10
#define CMD_BT_DISCONNECT 0x11
#define CMD_BT_PAIR_MODE 0x12
#define CMD_PING        0xF0

// Events (ESP32 -> S3)
#define EVT_BT_STATE    0x20
#define EVT_PLAY_STATUS 0x21
#define EVT_METADATA    0x22
#define EVT_DEVICE_NAME 0x23
#define EVT_VOLUME      0x24    // Volume level (0-127 AVRCP range)
#define EVT_POSITION    0x25    // Play position in ms (4 bytes LE)
#define EVT_PONG        0xF1
#define EVT_ACK         0xFE
#define EVT_ERROR       0xFF

// Metadata types
#define META_TITLE      0x01
#define META_ARTIST     0x02
#define META_ALBUM      0x03
#define META_DURATION   0x04    // Track duration in ms (as string)

/**
 * @brief Initialize UART protocol
 *
 * Configures UART and starts RX task.
 */
void uart_protocol_init(void);

/**
 * @brief Deinitialize UART protocol
 */
void uart_protocol_deinit(void);

/**
 * @brief Send Bluetooth state event to S3
 */
void uart_protocol_send_bt_state(uint8_t state);

/**
 * @brief Send play status event to S3
 */
void uart_protocol_send_play_status(uint8_t status);

/**
 * @brief Send metadata event to S3
 * @param type Metadata type (META_TITLE, META_ARTIST, META_ALBUM)
 * @param text Metadata text (null-terminated string)
 */
void uart_protocol_send_metadata(uint8_t type, const char *text);

/**
 * @brief Send device name event to S3
 */
void uart_protocol_send_device_name(const char *name);

/**
 * @brief Send ACK event to S3
 */
void uart_protocol_send_ack(uint8_t cmd_type);

/**
 * @brief Send error event to S3
 */
void uart_protocol_send_error(uint8_t code, const char *message);

/**
 * @brief Send pong (heartbeat response) to S3
 */
void uart_protocol_send_pong(void);

/**
 * @brief Send volume level event to S3
 * @param volume Volume level (0-127, AVRCP range)
 */
void uart_protocol_send_volume(uint8_t volume);

/**
 * @brief Send play position event to S3
 * @param position_ms Current play position in milliseconds
 */
void uart_protocol_send_position(uint32_t position_ms);

#ifdef __cplusplus
}
#endif

#endif /* UART_PROTOCOL_H */
