/**
 * @file uart_protocol.c
 * @brief UART protocol implementation
 *
 * Binary TLV protocol for ESP32 <-> ESP32-S3 communication.
 */

#include "uart_protocol.h"
#include "bt_avrcp.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "uart_proto";

// UART configuration
// Back to Gemini's suggestion: UART1 on GPIO23/18
// - ESP32 GPIO23 (TX) -> ESP32-S3 (RX)
// - ESP32 GPIO18 (RX) <- ESP32-S3 (TX)
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     23  // ESP32 TX
#define UART_RX_PIN     18  // ESP32 RX
#define UART_BAUD       1000000
#define UART_BUF_SIZE   512

// Frame parsing state
typedef enum {
    PARSE_WAIT_START,
    PARSE_TYPE,
    PARSE_LEN_LO,
    PARSE_LEN_HI,
    PARSE_PAYLOAD,
    PARSE_CRC,
    PARSE_END,
} parse_state_t;

static parse_state_t s_parse_state = PARSE_WAIT_START;
static uint8_t s_msg_type = 0;
static uint16_t s_msg_len = 0;
static uint16_t s_payload_idx = 0;
static uint8_t s_payload[256];
static uint8_t s_crc = 0;

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
    uint8_t header[4] = {
        FRAME_START,
        type,
        len & 0xFF,
        (len >> 8) & 0xFF
    };

    // Calculate CRC over type + length + payload
    uint8_t crc_data[3 + 256];
    crc_data[0] = type;
    crc_data[1] = len & 0xFF;
    crc_data[2] = (len >> 8) & 0xFF;
    if (payload && len > 0) {
        memcpy(&crc_data[3], payload, len);
    }
    uint8_t frame_crc = crc8(crc_data, 3 + len);

    uint8_t footer[2] = {frame_crc, FRAME_END};

    // Send frame
    uart_write_bytes(UART_NUM, header, sizeof(header));
    if (payload && len > 0) {
        uart_write_bytes(UART_NUM, payload, len);
    }
    uart_write_bytes(UART_NUM, footer, sizeof(footer));

    ESP_LOGI(TAG, "TX frame: type=0x%02X, len=%d", type, len);
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
            ESP_LOGI(TAG, "Deactivating Bluetooth...");
            bt_avrcp_deinit();
            uart_protocol_send_ack(CMD_BT_DEACTIVATE);
            break;

        case CMD_PING:
            uart_protocol_send_pong();
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", type);
            uart_protocol_send_error(0x01, "Unknown command");
            break;
    }
}

static void parse_byte(uint8_t byte)
{
    switch (s_parse_state) {
        case PARSE_WAIT_START:
            if (byte == FRAME_START) {
                s_parse_state = PARSE_TYPE;
            }
            break;

        case PARSE_TYPE:
            s_msg_type = byte;
            s_parse_state = PARSE_LEN_LO;
            break;

        case PARSE_LEN_LO:
            s_msg_len = byte;
            s_parse_state = PARSE_LEN_HI;
            break;

        case PARSE_LEN_HI:
            s_msg_len |= (uint16_t)byte << 8;
            s_payload_idx = 0;
            if (s_msg_len > 0 && s_msg_len <= sizeof(s_payload)) {
                s_parse_state = PARSE_PAYLOAD;
            } else if (s_msg_len == 0) {
                s_parse_state = PARSE_CRC;
            } else {
                ESP_LOGW(TAG, "Invalid length: %d", s_msg_len);
                s_parse_state = PARSE_WAIT_START;
            }
            break;

        case PARSE_PAYLOAD:
            s_payload[s_payload_idx++] = byte;
            if (s_payload_idx >= s_msg_len) {
                s_parse_state = PARSE_CRC;
            }
            break;

        case PARSE_CRC:
            s_crc = byte;
            s_parse_state = PARSE_END;
            break;

        case PARSE_END:
            if (byte == FRAME_END) {
                // Verify CRC
                uint8_t crc_data[3 + 256];
                crc_data[0] = s_msg_type;
                crc_data[1] = s_msg_len & 0xFF;
                crc_data[2] = (s_msg_len >> 8) & 0xFF;
                memcpy(&crc_data[3], s_payload, s_msg_len);
                uint8_t calc_crc = crc8(crc_data, 3 + s_msg_len);

                if (calc_crc == s_crc) {
                    process_message(s_msg_type, s_payload, s_msg_len);
                } else {
                    ESP_LOGW(TAG, "CRC mismatch: got 0x%02X, expected 0x%02X", s_crc, calc_crc);
                }
            } else {
                ESP_LOGW(TAG, "Missing end delimiter");
            }
            s_parse_state = PARSE_WAIT_START;
            break;
    }
}

static void uart_rx_task(void *arg)
{
    uint8_t buf[128];

    while (1) {
        int len = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                parse_byte(buf[i]);
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
        position_ms & 0xFF,
        (position_ms >> 8) & 0xFF,
        (position_ms >> 16) & 0xFF,
        (position_ms >> 24) & 0xFF
    };
    send_frame(EVT_POSITION, payload, sizeof(payload));
}
