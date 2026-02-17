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
