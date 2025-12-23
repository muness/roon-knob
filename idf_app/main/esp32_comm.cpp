/**
 * @file esp32_comm.cpp
 * @brief UART communication with ESP32 (Classic Bluetooth chip) using COBS framing
 */

#include "esp32_comm.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cobs.hpp"

#include <stdlib.h>
#include <string.h>
#include <span>

extern "C" {

static const char *TAG = "esp32_comm";

/* UART Configuration - verified working Dec 2025
 * - ESP32-S3 GPIO38 (TX) -> ESP32 GPIO18 (RX)
 * - ESP32-S3 GPIO48 (RX) <- ESP32 GPIO23 (TX)
 */
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     38  // S3 TX -> ESP32 RX (GPIO18)
#define UART_RX_PIN     48  // S3 RX <- ESP32 TX (GPIO23)
#define UART_BAUD       9600
#define UART_BUF_SIZE   512

/* COBS frame format: unencoded = Type(1) + Length(2LE) + Payload(0-256) + CRC8(1) 
 * COBS encodes this and appends 0x00 delimiter automatically */
#define MAX_UNENCODED_FRAME_SIZE  (1 + 2 + 256 + 1)  // 260 bytes
#define MAX_ENCODED_FRAME_SIZE    264  // 260 + ceil(260/254) + 2

/* Commands (S3 -> ESP32) */
#define CMD_PLAY        0x01
#define CMD_PAUSE       0x02
#define CMD_NEXT        0x03
#define CMD_PREV        0x04
#define CMD_VOL_UP      0x05
#define CMD_VOL_DOWN    0x06
#define CMD_SET_VOLUME  0x07
#define CMD_PLAY_PAUSE  0x08    // Toggle play/pause (for HID-only mode)
#define CMD_BT_CONNECT  0x10
#define CMD_BT_DISCONNECT 0x11
#define CMD_BT_PAIR_MODE 0x12
#define CMD_BT_ACTIVATE 0x13
#define CMD_BT_DEACTIVATE 0x14
#define CMD_BT_SET_MODE 0x15
#define CMD_BT_GET_MODE 0x16
#define CMD_PING        0xF0

/* Events (ESP32 -> S3) */
#define EVT_BT_STATE    0x20
#define EVT_PLAY_STATUS 0x21
#define EVT_METADATA    0x22
#define EVT_DEVICE_NAME 0x23
#define EVT_VOLUME      0x24
#define EVT_POSITION    0x25
#define EVT_BT_MODE     0x26
#define EVT_PONG        0xF1
#define EVT_ACK         0xFE
#define EVT_ERROR       0xFF

/* Heartbeat configuration */
#define HEARTBEAT_INTERVAL_MS   3000
#define HEARTBEAT_TIMEOUT_COUNT 3

/* COBS RX state - accumulate bytes until 0x00 delimiter */
static uint8_t s_rx_buffer[MAX_ENCODED_FRAME_SIZE];
static size_t s_rx_index = 0;

static esp32_bt_state_t s_bt_state = ESP32_BT_STATE_DISCONNECTED;
static esp32_play_state_t s_play_state = ESP32_PLAY_STATE_UNKNOWN;
static int s_missed_pongs = 0;
static bool s_healthy = false;

/* Metadata storage */
static char s_title[256] = {0};
static char s_artist[256] = {0};
static char s_album[256] = {0};
static char s_device_name[64] = {0};
static uint8_t s_volume = 64;  // Current volume (0-127, default 50%)
static uint32_t s_duration_ms = 0;  // Track duration in milliseconds
static uint32_t s_position_ms = 0;  // Current play position in milliseconds

static esp_timer_handle_t s_heartbeat_timer = NULL;
static TaskHandle_t s_rx_task_handle = NULL;

/* Callbacks */
static esp32_comm_bt_state_cb_t s_bt_state_cb = NULL;
static esp32_comm_play_state_cb_t s_play_state_cb = NULL;
static esp32_comm_metadata_cb_t s_metadata_cb = NULL;
static esp32_comm_device_name_cb_t s_device_name_cb = NULL;
static esp32_comm_health_cb_t s_health_cb = NULL;
static esp32_comm_volume_cb_t s_volume_cb = NULL;
static esp32_comm_position_cb_t s_position_cb = NULL;
static esp32_comm_bt_mode_cb_t s_bt_mode_cb = NULL;

static esp32_bt_mode_t s_bt_mode = ESP32_BT_MODE_PHONE;  // Default to phone mode

/* CRC-8 lookup table (polynomial 0x07) - same as ESP32 side */
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
    
    size_t unencoded_len = 3 + len + 1;  // Type + Length(2) + Payload + CRC
    
    /* COBS encode the frame */
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE];
    std::span<const uint8_t> input_span(unencoded, unencoded_len);
    std::span<uint8_t> output_span(encoded, sizeof(encoded));
    
    size_t encoded_len = espp::Cobs::encode_packet(input_span, output_span);
    
    if (encoded_len > 0) {
        /* Send COBS-encoded frame (includes 0x00 delimiter automatically) */
        uart_write_bytes(UART_NUM, encoded, encoded_len);
        ESP_LOGI(TAG, "TX: type=0x%02X, unencoded=%zu, encoded=%zu bytes", type, unencoded_len, encoded_len);
        
        /* Log first 16 bytes of encoded data for debugging */
        if (encoded_len <= 16) {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, encoded, encoded_len, ESP_LOG_INFO);
        } else {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, encoded, 16, ESP_LOG_INFO);
        }
    } else {
        ESP_LOGE(TAG, "COBS encode failed for type=0x%02X", type);
    }
}

static void process_message(uint8_t type, const uint8_t *payload, uint16_t len)
{
    ESP_LOGI(TAG, "process_message called: type=0x%02X, len=%d", type, len);

    switch (type) {
        case EVT_BT_STATE:
            if (len >= 1) {
                s_bt_state = (esp32_bt_state_t)payload[0];
                ESP_LOGI(TAG, "BT state: %d", s_bt_state);
                if (s_bt_state_cb) {
                    s_bt_state_cb(s_bt_state);
                }
            }
            break;

        case EVT_PLAY_STATUS:
            if (len >= 1) {
                s_play_state = (esp32_play_state_t)payload[0];
                ESP_LOGI(TAG, "Play state: %d", s_play_state);
                if (s_play_state_cb) {
                    s_play_state_cb(s_play_state);
                }
            }
            break;

        case EVT_METADATA:
            if (len >= 2) {
                esp32_meta_type_t meta_type = (esp32_meta_type_t)payload[0];
                /* Null-terminate the string */
                char text[256];
                size_t text_len = len - 1;
                if (text_len > sizeof(text) - 1) {
                    text_len = sizeof(text) - 1;
                }
                memcpy(text, &payload[1], text_len);
                text[text_len] = '\0';

                /* Store in appropriate buffer */
                switch (meta_type) {
                    case ESP32_META_TITLE:
                        strncpy(s_title, text, sizeof(s_title) - 1);
                        s_title[sizeof(s_title) - 1] = '\0';
                        break;
                    case ESP32_META_ARTIST:
                        strncpy(s_artist, text, sizeof(s_artist) - 1);
                        s_artist[sizeof(s_artist) - 1] = '\0';
                        break;
                    case ESP32_META_ALBUM:
                        strncpy(s_album, text, sizeof(s_album) - 1);
                        s_album[sizeof(s_album) - 1] = '\0';
                        break;
                    case ESP32_META_DURATION:
                        // Duration comes as a string in milliseconds
                        s_duration_ms = (uint32_t)atol(text);
                        ESP_LOGI(TAG, "Track duration: %lu ms", (unsigned long)s_duration_ms);
                        break;
                }

                // Only log non-duration metadata (duration already logged above)
                if (meta_type != ESP32_META_DURATION) {
                    ESP_LOGI(TAG, "Metadata[%d]: %s", meta_type, text);
                }
                if (s_metadata_cb) {
                    s_metadata_cb(meta_type, text);
                }
            }
            break;

        case EVT_DEVICE_NAME:
            if (len > 0) {
                size_t name_len = len;
                if (name_len > sizeof(s_device_name) - 1) {
                    name_len = sizeof(s_device_name) - 1;
                }
                memcpy(s_device_name, payload, name_len);
                s_device_name[name_len] = '\0';
                ESP_LOGI(TAG, "Device name: %s", s_device_name);
                if (s_device_name_cb) {
                    s_device_name_cb(s_device_name);
                }
            }
            break;

        case EVT_VOLUME:
            if (len >= 1) {
                s_volume = payload[0];
                ESP_LOGI(TAG, "Volume: %d (%.0f%%)", s_volume, s_volume * 100.0 / 127.0);
                if (s_volume_cb) {
                    s_volume_cb(s_volume);
                }
            }
            break;

        case EVT_POSITION:
            if (len >= 4) {
                // Position is 4 bytes little-endian
                s_position_ms = payload[0] |
                               ((uint32_t)payload[1] << 8) |
                               ((uint32_t)payload[2] << 16) |
                               ((uint32_t)payload[3] << 24);
                ESP_LOGD(TAG, "Position: %lu ms", (unsigned long)s_position_ms);
                if (s_position_cb) {
                    s_position_cb(s_position_ms);
                }
            }
            break;

        case EVT_BT_MODE:
            if (len >= 1) {
                s_bt_mode = (esp32_bt_mode_t)payload[0];
                const char *mode_str = (s_bt_mode == ESP32_BT_MODE_PHONE) ? "Phone" :
                                      (s_bt_mode == ESP32_BT_MODE_CONTROLLER) ? "Controller" :
                                      "Controller (no hack)";
                ESP_LOGI(TAG, "BT Mode: %s", mode_str);
                if (s_bt_mode_cb) {
                    s_bt_mode_cb(s_bt_mode);
                }
            }
            break;

        case EVT_PONG:
            ESP_LOGI(TAG, "PONG handler called, missed_pongs was %d, resetting to 0", s_missed_pongs);
            s_missed_pongs = 0;
            if (!s_healthy) {
                s_healthy = true;
                if (s_health_cb) {
                    s_health_cb(true);
                }
            }
            break;

        case EVT_ACK:
            if (len >= 1) {
                ESP_LOGD(TAG, "ACK for command 0x%02X", payload[0]);
            }
            break;

        case EVT_ERROR:
            if (len >= 1) {
                uint8_t code = payload[0];
                char msg[256] = {0};
                if (len > 1) {
                    size_t msg_len = len - 1;
                    if (msg_len > sizeof(msg) - 1) {
                        msg_len = sizeof(msg) - 1;
                    }
                    memcpy(msg, &payload[1], msg_len);
                }
                ESP_LOGW(TAG, "Error from ESP32: code=%d, msg=%s", code, msg);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown event type: 0x%02X", type);
            break;
    }
}

static void process_cobs_frame()
{
    if (s_rx_index == 0) {
        return;  // Empty frame, ignore
    }
    
    /* COBS decode the frame */
    uint8_t decoded[MAX_UNENCODED_FRAME_SIZE];
    std::span<const uint8_t> encoded_span(s_rx_buffer, s_rx_index);
    std::span<uint8_t> decoded_span(decoded, sizeof(decoded));
    
    size_t decoded_len = espp::Cobs::decode_packet(encoded_span, decoded_span);
    
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
        ESP_LOGW(TAG, "CRC mismatch: got=0x%02X, calc=0x%02X", received_crc, calculated_crc);
        return;
    }
    
    /* Process valid frame */
    const uint8_t *payload = (payload_len > 0) ? &decoded[3] : nullptr;
    process_message(type, payload, payload_len);
}

static void uart_rx_task(void *arg)
{
    uint8_t buf[128];

    ESP_LOGI(TAG, "UART RX task started (COBS mode)");

    while (1) {
        int len = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                if (buf[i] == 0x00) {
                    /* Frame delimiter - process accumulated frame */
                    process_cobs_frame();
                    s_rx_index = 0;  // Reset for next frame
                } else {
                    /* Accumulate encoded data */
                    if (s_rx_index < sizeof(s_rx_buffer)) {
                        s_rx_buffer[s_rx_index++] = buf[i];
                    } else {
                        ESP_LOGW(TAG, "RX buffer overflow, resetting");
                        s_rx_index = 0;
                    }
                }
            }
        }
    }
}

static void heartbeat_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Heartbeat timer fired, missed_pongs before=%d", s_missed_pongs);
    
    /* Send ping */
    send_frame(CMD_PING, NULL, 0);

    /* Check for missed pongs */
    s_missed_pongs++;
    ESP_LOGI(TAG, "After increment, missed_pongs=%d", s_missed_pongs);
    
    if (s_missed_pongs >= HEARTBEAT_TIMEOUT_COUNT && s_healthy) {
        s_healthy = false;
        ESP_LOGW(TAG, "ESP32 communication lost (missed %d pongs)", s_missed_pongs);
        if (s_health_cb) {
            s_health_cb(false);
        }
    }
}

void esp32_comm_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP32 communication on TX=%d, RX=%d @ %d baud",
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

    /* Flush UART buffers to clear boot-time garbage */
    uart_flush_input(UART_NUM);
    uart_flush(UART_NUM);
    
    /* Send burst of 0x00 delimiters to help receiver sync */
    /* This clears any partial frame state the receiver might have from boot noise */
    ESP_LOGI(TAG, "Sending 0x00 burst to clear receiver state...");
    uint8_t zero_burst[20];
    memset(zero_burst, 0x00, sizeof(zero_burst));
    uart_write_bytes(UART_NUM, zero_burst, sizeof(zero_burst));
    vTaskDelay(pdMS_TO_TICKS(50));  /* Allow time for burst to transmit */

    /* Start RX task */
    xTaskCreate(uart_rx_task, "esp32_rx", 4096, NULL, 10, &s_rx_task_handle);

    /* Start heartbeat timer */
    esp_timer_create_args_t timer_args = {
        .callback = heartbeat_timer_cb,
        .name = "esp32_hb",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_heartbeat_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_heartbeat_timer, HEARTBEAT_INTERVAL_MS * 1000));

    ESP_LOGI(TAG, "ESP32 communication initialized");
}

void esp32_comm_deinit(void)
{
    if (s_heartbeat_timer) {
        esp_timer_stop(s_heartbeat_timer);
        esp_timer_delete(s_heartbeat_timer);
        s_heartbeat_timer = NULL;
    }

    if (s_rx_task_handle) {
        vTaskDelete(s_rx_task_handle);
        s_rx_task_handle = NULL;
    }

    uart_driver_delete(UART_NUM);
}

bool esp32_comm_is_healthy(void)
{
    return s_healthy;
}

esp32_bt_state_t esp32_comm_get_bt_state(void)
{
    return s_bt_state;
}

esp32_play_state_t esp32_comm_get_play_state(void)
{
    return s_play_state;
}

const char *esp32_comm_get_title(void)
{
    return s_title;
}

const char *esp32_comm_get_artist(void)
{
    return s_artist;
}

const char *esp32_comm_get_album(void)
{
    return s_album;
}

const char *esp32_comm_get_device_name(void)
{
    return s_device_name;
}

uint8_t esp32_comm_get_volume(void)
{
    return s_volume;
}

uint32_t esp32_comm_get_duration(void)
{
    return s_duration_ms;
}

uint32_t esp32_comm_get_position(void)
{
    return s_position_ms;
}

esp32_bt_mode_t esp32_comm_get_bt_mode(void)
{
    return s_bt_mode;
}

/* Command functions */

void esp32_comm_send_play(void)
{
    ESP_LOGI(TAG, "Sending PLAY command");
    send_frame(CMD_PLAY, NULL, 0);
}

void esp32_comm_send_pause(void)
{
    ESP_LOGI(TAG, "Sending PAUSE command");
    send_frame(CMD_PAUSE, NULL, 0);
}

void esp32_comm_send_play_pause(void)
{
    ESP_LOGI(TAG, "Sending PLAY_PAUSE toggle command");
    send_frame(CMD_PLAY_PAUSE, NULL, 0);
}

void esp32_comm_send_next(void)
{
    ESP_LOGI(TAG, "Sending NEXT command");
    send_frame(CMD_NEXT, NULL, 0);
}

void esp32_comm_send_prev(void)
{
    ESP_LOGI(TAG, "Sending PREV command");
    send_frame(CMD_PREV, NULL, 0);
}

void esp32_comm_send_vol_up(void)
{
    ESP_LOGI(TAG, "Sending VOL_UP command");
    send_frame(CMD_VOL_UP, NULL, 0);
}

void esp32_comm_send_vol_down(void)
{
    ESP_LOGI(TAG, "Sending VOL_DOWN command");
    send_frame(CMD_VOL_DOWN, NULL, 0);
}

void esp32_comm_send_set_volume(uint8_t volume)
{
    ESP_LOGI(TAG, "Sending SET_VOLUME command: %d", volume);
    send_frame(CMD_SET_VOLUME, &volume, 1);
}

void esp32_comm_send_bt_connect(void)
{
    ESP_LOGI(TAG, "Sending BT_CONNECT command");
    send_frame(CMD_BT_CONNECT, NULL, 0);
}

void esp32_comm_send_bt_disconnect(void)
{
    ESP_LOGI(TAG, "Sending BT_DISCONNECT command");
    send_frame(CMD_BT_DISCONNECT, NULL, 0);
}

void esp32_comm_send_bt_pair_mode(void)
{
    ESP_LOGI(TAG, "Sending BT_PAIR_MODE command");
    send_frame(CMD_BT_PAIR_MODE, NULL, 0);
}

void esp32_comm_send_bt_activate(void)
{
    ESP_LOGI(TAG, "Sending BT_ACTIVATE command");
    send_frame(CMD_BT_ACTIVATE, NULL, 0);
}

void esp32_comm_send_bt_deactivate(void)
{
    ESP_LOGI(TAG, "Sending BT_DEACTIVATE command");
    send_frame(CMD_BT_DEACTIVATE, NULL, 0);
}

void esp32_comm_send_bt_set_mode(esp32_bt_mode_t mode)
{
    ESP_LOGI(TAG, "Sending BT_SET_MODE command: %d", mode);
    uint8_t payload = (uint8_t)mode;
    send_frame(CMD_BT_SET_MODE, &payload, 1);
}

void esp32_comm_send_bt_get_mode(void)
{
    ESP_LOGI(TAG, "Sending BT_GET_MODE command");
    send_frame(CMD_BT_GET_MODE, NULL, 0);
}

/* Callback registration */

void esp32_comm_set_bt_state_cb(esp32_comm_bt_state_cb_t cb)
{
    s_bt_state_cb = cb;
}

void esp32_comm_set_play_state_cb(esp32_comm_play_state_cb_t cb)
{
    s_play_state_cb = cb;
}

void esp32_comm_set_metadata_cb(esp32_comm_metadata_cb_t cb)
{
    s_metadata_cb = cb;
}

void esp32_comm_set_device_name_cb(esp32_comm_device_name_cb_t cb)
{
    s_device_name_cb = cb;
}

void esp32_comm_set_health_cb(esp32_comm_health_cb_t cb)
{
    s_health_cb = cb;
}

void esp32_comm_set_volume_cb(esp32_comm_volume_cb_t cb)
{
    s_volume_cb = cb;
}

void esp32_comm_set_position_cb(esp32_comm_position_cb_t cb)
{
    s_position_cb = cb;
}

void esp32_comm_set_bt_mode_cb(esp32_comm_bt_mode_cb_t cb)
{
    s_bt_mode_cb = cb;
}

bool esp32_comm_wait_for_sync(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Starting UART handshake (timeout=%lu ms)", timeout_ms);
    
    uint32_t start_time = esp_timer_get_time() / 1000;  // Convert to ms
    int retry_count = 0;
    const int max_retries = 10;
    const int retry_delay_ms = 300;  // Start with 300ms between retries
    
    while (retry_count < max_retries) {
        uint32_t elapsed = (esp_timer_get_time() / 1000) - start_time;
        if (elapsed >= timeout_ms) {
            ESP_LOGW(TAG, "UART handshake timeout after %lu ms", elapsed);
            return false;
        }
        
        // Reset missed_pongs counter and send PING
        s_missed_pongs = max_retries;  // Set high to detect PONG
        send_frame(CMD_PING, NULL, 0);
        ESP_LOGI(TAG, "Handshake attempt %d: sent PING", retry_count + 1);
        
        // Wait for PONG response (check every 50ms)
        int wait_iterations = retry_delay_ms / 50;
        for (int i = 0; i < wait_iterations; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // Check if PONG was received (s_missed_pongs will be reset to 0 by PONG handler)
            if (s_missed_pongs == 0) {
                ESP_LOGI(TAG, "UART handshake successful after %d attempts", retry_count + 1);
                return true;
            }
            
            // Check timeout
            elapsed = (esp_timer_get_time() / 1000) - start_time;
            if (elapsed >= timeout_ms) {
                ESP_LOGW(TAG, "UART handshake timeout during wait");
                return false;
            }
        }
        
        retry_count++;
        ESP_LOGD(TAG, "No PONG received, retry %d/%d", retry_count, max_retries);
    }
    
    ESP_LOGW(TAG, "UART handshake failed after %d retries", max_retries);
    return false;
}

} // extern "C"
