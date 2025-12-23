/**
 * @file uart_protocol.cpp
 * @brief UART protocol implementation with COBS framing
 *
 * Binary TLV protocol for ESP32 <-> ESP32-S3 communication.
 * Uses COBS encoding with 0x00 as frame delimiter for reliable synchronization.
 */

#include "uart_protocol.h"
#include "bt_avrcp.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <span>
#include "cobs.hpp"

extern "C" {

static const char *TAG = "uart_proto";

// UART configuration
// Back to Gemini's suggestion: UART1 on GPIO23/18
// - ESP32 GPIO23 (TX) -> ESP32-S3 (RX)
// - ESP32 GPIO18 (RX) <- ESP32-S3 (TX)
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     23  // ESP32 TX
#define UART_RX_PIN     18  // ESP32 RX
#define UART_BAUD       115200
#define UART_BUF_SIZE   512

// COBS frame buffer sizes
// Unencoded frame: Type(1) + Length(2) + Payload(0-256) + CRC(1) = max 260 bytes
// COBS worst case adds 1 byte per 254 bytes + 1 overhead byte + 1 delimiter (0x00)
#define MAX_UNENCODED_FRAME_SIZE 260
#define MAX_ENCODED_FRAME_SIZE   264

// RX buffer for accumulating COBS-encoded bytes until delimiter
static uint8_t s_rx_buffer[MAX_ENCODED_FRAME_SIZE];
static size_t s_rx_index = 0;

// CRC-8 lookup table (polynomial 0x07)
static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    while (len--) {
        crc = crc8_table[crc ^ *data++];
    }
    return crc;
}

static void send_frame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    /* Build unencoded frame: Type + Length + Payload + CRC */
    uint8_t unencoded[MAX_UNENCODED_FRAME_SIZE];
    unencoded[0] = type;
    unencoded[1] = len & 0xFF;
    unencoded[2] = (len >> 8) & 0xFF;
    
    if (payload && len > 0) {
        memcpy(&unencoded[3], payload, len);
    }
    
    /* Calculate CRC over type + length + payload */
    uint8_t frame_crc = crc8(unencoded, 3 + len);
    unencoded[3 + len] = frame_crc;
    
    size_t unencoded_len = 3 + len + 1;
    
    /* COBS encode the frame */
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE];
    std::span<const uint8_t> input_span(unencoded, unencoded_len);
    std::span<uint8_t> output_span(encoded, sizeof(encoded));
    
    size_t encoded_len = espp::Cobs::encode_packet(input_span, output_span);
    
    if (encoded_len > 0) {
        uart_write_bytes(UART_NUM, encoded, encoded_len);
        ESP_LOGD(TAG, "Sent COBS frame: type=0x%02X, len=%d, encoded=%zu", type, len, encoded_len);
    } else {
        ESP_LOGW(TAG, "COBS encode failed for frame type=0x%02X", type);
    }
}

static void process_message(uint8_t type, const uint8_t *payload, uint16_t len)
{
    ESP_LOGI(TAG, "Received message: type=0x%02X, len=%d", type, len);

    switch (type) {
        case CMD_PLAY:
            bt_avrcp_play();
            uart_protocol_send_ack(CMD_PLAY);
            break;

        case CMD_PAUSE:
            bt_avrcp_pause();
            uart_protocol_send_ack(CMD_PAUSE);
            break;

        case CMD_PLAY_PAUSE:
            bt_avrcp_play_pause();
            uart_protocol_send_ack(CMD_PLAY_PAUSE);
            break;

        case CMD_NEXT:
            bt_avrcp_next();
            uart_protocol_send_ack(CMD_NEXT);
            break;

        case CMD_PREV:
            bt_avrcp_prev();
            uart_protocol_send_ack(CMD_PREV);
            break;

        case CMD_VOL_UP:
            bt_avrcp_vol_up();
            uart_protocol_send_ack(CMD_VOL_UP);
            break;

        case CMD_VOL_DOWN:
            bt_avrcp_vol_down();
            uart_protocol_send_ack(CMD_VOL_DOWN);
            break;

        case CMD_SET_VOLUME:
            if (len >= 1) {
                bt_avrcp_set_volume(payload[0]);
                uart_protocol_send_ack(CMD_SET_VOLUME);
            }
            break;

        case CMD_BT_CONNECT:
            bt_avrcp_connect();
            uart_protocol_send_ack(CMD_BT_CONNECT);
            break;

        case CMD_BT_DISCONNECT:
            bt_avrcp_disconnect();
            uart_protocol_send_ack(CMD_BT_DISCONNECT);
            break;

        case CMD_BT_PAIR_MODE:
            bt_avrcp_enter_pairing_mode();
            uart_protocol_send_ack(CMD_BT_PAIR_MODE);
            break;

        case CMD_BT_ACTIVATE:
            ESP_LOGI(TAG, "Activating Bluetooth...");
            bt_avrcp_init();
            uart_protocol_send_ack(CMD_BT_ACTIVATE);
            break;

        case CMD_BT_DEACTIVATE:
            ESP_LOGI(TAG, "Deactivating Bluetooth and entering deep sleep...");
            bt_avrcp_deinit();
            uart_protocol_send_ack(CMD_BT_DEACTIVATE);
            vTaskDelay(pdMS_TO_TICKS(100));  // Allow ACK to be sent
            
            // Configure UART wakeup - wake on CMD_BT_ACTIVATE
            uart_set_wakeup_threshold(UART_NUM, 3);  // Wake after 3 edges
            esp_sleep_enable_uart_wakeup(UART_NUM);
            
            ESP_LOGI(TAG, "Entering deep sleep (wake on UART)");
            esp_deep_sleep_start();
            break;

        case CMD_BT_SET_MODE:
            if (len != 1) {
                ESP_LOGW(TAG, "CMD_BT_SET_MODE: Invalid payload length %d", len);
                uart_protocol_send_error(0x02, "Invalid payload");
            } else {
                uint8_t mode = payload[0];
                ESP_LOGI(TAG, "Setting BT mode to %d", mode);
                bt_avrcp_set_mode((bt_mode_t)mode);
                uart_protocol_send_ack(CMD_BT_SET_MODE);
                uart_protocol_send_bt_mode(mode);
            }
            break;

        case CMD_BT_GET_MODE: {
            uint8_t mode = (uint8_t)bt_avrcp_get_mode();
            ESP_LOGI(TAG, "Current BT mode: %d", mode);
            uart_protocol_send_bt_mode(mode);
            break;
        }

        case CMD_PING:
            uart_protocol_send_pong();
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", type);
            uart_protocol_send_error(0x01, "Unknown command");
            break;
    }
}

static void process_cobs_frame()
{
    if (s_rx_index == 0) {
        return;
    }
    
    ESP_LOGI(TAG, "RX: Processing frame, encoded=%zu bytes", s_rx_index);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, s_rx_buffer, s_rx_index > 16 ? 16 : s_rx_index, ESP_LOG_INFO);
    
    /* COBS decode the frame */
    uint8_t decoded[MAX_UNENCODED_FRAME_SIZE];
    std::span<const uint8_t> encoded_span(s_rx_buffer, s_rx_index);
    std::span<uint8_t> decoded_span(decoded, sizeof(decoded));
    
    size_t decoded_len = espp::Cobs::decode_packet(encoded_span, decoded_span);
    
    ESP_LOGI(TAG, "RX: Decoded %zu bytes", decoded_len);
    if (decoded_len > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, decoded, decoded_len > 16 ? 16 : decoded_len, ESP_LOG_INFO);
    }
    
    if (decoded_len < 4) {
        ESP_LOGW(TAG, "COBS decoded frame too short: %zu bytes", decoded_len);
        return;
    }
    
    /* Parse decoded frame: Type + Length + Payload + CRC */
    uint8_t type = decoded[0];
    uint16_t payload_len = decoded[1] | ((uint16_t)decoded[2] << 8);
    
    if (decoded_len != 3 + payload_len + 1) {
        ESP_LOGW(TAG, "Frame length mismatch: decoded=%zu, expected=%d", decoded_len, 3 + payload_len + 1);
        return;
    }
    
    /* Verify CRC */
    uint8_t received_crc = decoded[3 + payload_len];
    uint8_t calculated_crc = crc8(decoded, 3 + payload_len);
    
    if (received_crc != calculated_crc) {
        ESP_LOGW(TAG, "CRC mismatch: got 0x%02X, expected 0x%02X", received_crc, calculated_crc);
        return;
    }
    
    const uint8_t *payload = (payload_len > 0) ? &decoded[3] : nullptr;
    ESP_LOGD(TAG, "RX frame: type=0x%02X, len=%d", type, payload_len);
    process_message(type, payload, payload_len);
}

static void uart_rx_task(void *arg)
{
    uint8_t buf[128];

    while (1) {
        int len = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len > 0) {
            /* Log every byte received for physical layer debugging */
            ESP_LOGI(TAG, "RX raw: %d bytes", len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len > 32 ? 32 : len, ESP_LOG_INFO);
            
            for (int i = 0; i < len; i++) {
                if (buf[i] == 0x00) {
                    /* Frame delimiter - process accumulated frame */
                    process_cobs_frame();
                    /* ALWAYS reset state after delimiter, regardless of frame validity */
                    s_rx_index = 0;
                } else {
                    /* Accumulate non-delimiter bytes */
                    if (s_rx_index < sizeof(s_rx_buffer)) {
                        s_rx_buffer[s_rx_index++] = buf[i];
                    } else {
                        /* Max frame size exceeded - discard and wait for delimiter */
                        ESP_LOGW(TAG, "RX buffer overflow at %zu bytes, waiting for delimiter", s_rx_index);
                        /* Don't reset s_rx_index - keep it maxed so we keep discarding until 0x00 */
                    }
                }
            }
        }
    }
}

void uart_protocol_init(void)
{
    ESP_LOGI(TAG, "Initializing UART protocol on pins TX=%d, RX=%d @ %d baud",
             UART_TX_PIN, UART_RX_PIN, UART_BAUD);

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Flush UART buffers to clear boot-time garbage
    uart_flush_input(UART_NUM);
    uart_flush(UART_NUM);
    
    ESP_LOGI(TAG, "Draining UART for 300ms to discard boot noise...");
    uint8_t drain_buf[128];
    int64_t drain_start = esp_timer_get_time();
    int total_drained = 0;
    while ((esp_timer_get_time() - drain_start) < 300000) {  // 300ms
        int len = uart_read_bytes(UART_NUM, drain_buf, sizeof(drain_buf), pdMS_TO_TICKS(50));
        if (len > 0) {
            total_drained += len;
        }
    }
    if (total_drained > 0) {
        ESP_LOGI(TAG, "Drained %d bytes of boot noise", total_drained);
    }

    // Start RX task
    xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 10, NULL);

    ESP_LOGI(TAG, "UART protocol initialized");
}

void uart_protocol_deinit(void)
{
    uart_driver_delete(UART_NUM);
}

void uart_protocol_send_bt_state(uint8_t state)
{
    send_frame(EVT_BT_STATE, &state, 1);
}

void uart_protocol_send_play_status(uint8_t status)
{
    send_frame(EVT_PLAY_STATUS, &status, 1);
}

void uart_protocol_send_metadata(uint8_t type, const char *text)
{
    if (!text) return;

    size_t text_len = strlen(text);
    if (text_len > 254) text_len = 254;  // Leave room for type byte

    uint8_t payload[255];
    payload[0] = type;
    memcpy(&payload[1], text, text_len);

    send_frame(EVT_METADATA, payload, 1 + text_len);
}

void uart_protocol_send_device_name(const char *name)
{
    if (!name) return;

    size_t len = strlen(name);
    if (len > 255) len = 255;

    send_frame(EVT_DEVICE_NAME, (const uint8_t *)name, len);
}

void uart_protocol_send_ack(uint8_t cmd_type)
{
    send_frame(EVT_ACK, &cmd_type, 1);
}

void uart_protocol_send_error(uint8_t code, const char *message)
{
    uint8_t payload[256];
    payload[0] = code;

    size_t msg_len = message ? strlen(message) : 0;
    if (msg_len > 254) msg_len = 254;

    if (msg_len > 0) {
        memcpy(&payload[1], message, msg_len);
    }

    send_frame(EVT_ERROR, payload, 1 + msg_len);
}

void uart_protocol_send_pong(void)
{
    send_frame(EVT_PONG, NULL, 0);
}

void uart_protocol_send_volume(uint8_t volume)
{
    send_frame(EVT_VOLUME, &volume, 1);
}

void uart_protocol_send_position(uint32_t position_ms)
{
    // Send position as 4 bytes little-endian
    uint8_t payload[4] = {
        (uint8_t)(position_ms & 0xFF),
        (uint8_t)((position_ms >> 8) & 0xFF),
        (uint8_t)((position_ms >> 16) & 0xFF),
        (uint8_t)((position_ms >> 24) & 0xFF)
    };
    send_frame(EVT_POSITION, payload, sizeof(payload));
}

void uart_protocol_send_bt_mode(uint8_t mode)
{
    send_frame(EVT_BT_MODE, &mode, 1);
}

} // extern "C"
