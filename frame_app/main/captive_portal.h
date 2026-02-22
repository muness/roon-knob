#pragma once

#include <stdbool.h>

// Start the captive portal HTTP server (call when AP mode starts)
void captive_portal_start(void);

// Start STA-mode web server (zone picker + BLE config)
void captive_portal_start_sta(void);

// Stop the HTTP server (works for both AP and STA modes)
void captive_portal_stop(void);

// Check if web server is running
bool captive_portal_is_running(void);
