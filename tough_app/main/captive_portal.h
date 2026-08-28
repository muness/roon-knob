#pragma once

#include <stdbool.h>

// Start the captive portal HTTP server (call when AP mode starts)
// Returns true only when the AP HTTP handlers and captive DNS are ready.
bool captive_portal_start(void);

// Start STA-mode web server (zone picker)
bool captive_portal_start_sta(void);

// Stop the HTTP server (works for both AP and STA modes)
void captive_portal_stop(void);

// Check if the AP captive portal is running. This deliberately excludes the
// connected-mode STA web server.
bool captive_portal_is_running(void);

// Check whether the connected-mode STA web server is running.
bool captive_portal_is_sta_running(void);
