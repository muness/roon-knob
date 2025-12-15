#pragma once

#include "rk_cfg.h"
#include "ui.h"
#include <stdbool.h>
#include <stddef.h>

void roon_client_start(const rk_cfg_t *cfg);
void roon_client_handle_input(ui_input_event_t event);
void roon_client_handle_volume_rotation(int ticks);  // Velocity-sensitive volume control
void roon_client_set_network_ready(bool ready);
const char* roon_client_get_artwork_url(char *url_buf, size_t buf_len, int width, int height);
bool roon_client_is_ready_for_art_mode(void);

// Bridge connection status (mirrors WiFi retry pattern for consistent UX)
void roon_client_set_device_ip(const char *ip);  // Call when WiFi gets IP
int roon_client_get_bridge_retry_count(void);    // Current retry attempt (0 = connected)
int roon_client_get_bridge_retry_max(void);      // Max retries before showing recovery info
bool roon_client_get_bridge_url(char *buf, size_t len);  // Get configured bridge URL
bool roon_client_is_bridge_connected(void);      // True if bridge is responding
