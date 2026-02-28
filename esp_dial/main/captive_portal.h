#pragma once

#include <stdbool.h>

// Start the captive portal HTTP server (call when AP mode starts)
void captive_portal_start(void);

// Stop the captive portal HTTP server (call when leaving AP mode)
void captive_portal_stop(void);

// Check if captive portal is running
bool captive_portal_is_running(void);
