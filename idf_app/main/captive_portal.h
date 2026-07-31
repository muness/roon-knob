#pragma once

#include <stdbool.h>

// Start the captive portal HTTP server (call when AP mode starts)
// Returns true only when the AP HTTP handlers and captive DNS are ready.
bool captive_portal_start(void);

// Internal handoff primitives; caller must hold http_server_lifecycle_lock().
bool captive_portal_start_locked(void);
void captive_portal_stop_locked(void);
bool captive_portal_is_running_locked(void);

// Stop the captive portal HTTP server (call when leaving AP mode)
void captive_portal_stop(void);

// Check if the AP captive portal (not merely another HTTP service) is running.
bool captive_portal_is_running(void);
