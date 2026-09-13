#pragma once

#include <stdbool.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Register the shared /power-debug GET and /power-debug/sleep POST routes. */
bool power_debug_web_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
