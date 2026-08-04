#pragma once

#include <stddef.h>

#define PLATFORM_HTTP_JSON_MAX_BYTES (256U * 1024U)

int platform_http_get(const char *url, char **out, size_t *out_len);
/* Fetches a response into PSRAM with a hard ceiling. Adaptive-screen callers
 * should select the smallest limit their schema permits. */
int platform_http_get_bounded(const char *url, size_t max_bytes,
                              char **out, size_t *out_len);
int platform_http_get_image(const char *url, char **out, size_t *out_len);
int platform_http_post_json(const char *url, const char *json, char **out, size_t *out_len);
void platform_http_free(char *p);

/**
 * @brief Get the unique knob ID (MAC address based)
 * @param out Buffer to write the knob ID (13 bytes minimum: 12 hex + null)
 * @param len Buffer length
 */
void platform_http_get_knob_id(char *out, size_t len);
