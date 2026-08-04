#pragma once

#include <stdbool.h>

// Start HTTP config server on port 80 (call when connected to WiFi)
void config_server_start(void);

// Stop the config server
void config_server_stop(void);

// Internal handoff primitive; caller must hold http_server_lifecycle_lock().
void config_server_stop_locked(void);

// Check if config server is running
bool config_server_is_running(void);
