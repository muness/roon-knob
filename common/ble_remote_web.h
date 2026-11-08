// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

#include <esp_http_server.h>
#include <stdbool.h>

#define BLE_REMOTE_WEB_ROUTE_COUNT 9

/* Register on the existing STA settings server. settings_path must be static. */
bool ble_remote_web_register(httpd_handle_t server, const char *settings_path);
