#pragma once

#include "rk_cfg.h"
#include "ui.h"
#include <stdbool.h>
#include <stddef.h>

void bridge_client_start(const rk_cfg_t *cfg);
void bridge_client_handle_input(ui_input_event_t event);
void bridge_client_handle_volume_rotation(
    int ticks); // Velocity-sensitive volume control
void bridge_client_set_network_ready(bool ready);
const char *bridge_client_get_artwork_url(char *url_buf, size_t buf_len,
                                          int width, int height,
                                          int clip_radius);
bool bridge_client_is_ready_for_art_mode(void);

// Bridge connection status (mirrors WiFi retry pattern for consistent UX)
void bridge_client_set_device_ip(const char *ip); // Call when WiFi gets IP
int bridge_client_get_bridge_retry_count(
    void); // Current retry attempt (0 = connected)
int bridge_client_get_bridge_retry_max(
    void); // Max retries before showing recovery info
bool bridge_client_get_bridge_url(char *buf,
                                  size_t len); // Get configured bridge URL
bool bridge_client_is_bridge_connected(void);  // True if bridge is responding
bool bridge_client_is_bridge_mdns(
    void); // True if bridge was discovered via mDNS (persisted)

// ── UDP fast-path wire format ───────────────────────────────────────────────

#include <stdint.h>

#define UDP_FAST_MAGIC 0x524B
#define UDP_FAST_PORT_OFFSET 1 // bridge_port + 1
#define UDP_REQUEST_SIZE 54
#define UDP_RESPONSE_SIZE 48

#define UDP_CMD_PLAY_PAUSE  1
#define UDP_CMD_NEXT        2
#define UDP_CMD_PREV        3
#define UDP_CMD_STOP        4
#define UDP_CMD_VOLUME_SET  5
#define UDP_CMD_SIZE        36
#define UDP_CMD_VOL_SIZE    40

typedef struct __attribute__((packed)) {
    uint16_t magic;       // 0x524B LE
    uint8_t  cmd;         // command code
    uint8_t  _pad;        // reserved
    char     zone_id[32]; // null-terminated
    float    value;       // f32 LE (for volume)
} udp_command_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;       // 0x524B LE
    uint8_t  sha[20];     // manifest SHA (null-terminated in 20-byte field)
    char     zone_id[32]; // zone_id (null-terminated)
} udp_fast_request_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;          // 0x524B LE
    uint8_t  version;        // 1
    uint8_t  flags;          // bit 0: playing, 1-4: transport
    uint8_t  sha[20];        // current SHA
    float    volume;         // LE
    float    volume_min;     // LE
    float    volume_max;     // LE
    float    volume_step;    // LE
    int32_t  seek_position;  // -1 = unknown
    uint32_t length;         // 0 = unknown
} udp_fast_response_t;

// Flag bit positions
#define UDP_FLAG_PLAYING  (1 << 0)
#define UDP_FLAG_PLAY_OK  (1 << 1)
#define UDP_FLAG_PAUSE_OK (1 << 2)
#define UDP_FLAG_NEXT_OK  (1 << 3)
#define UDP_FLAG_PREV_OK  (1 << 4)
