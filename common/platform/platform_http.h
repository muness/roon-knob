#pragma once

#include <stddef.h>

int platform_http_get(const char *url, char **out, size_t *out_len);
int platform_http_post_json(const char *url, const char *json, char **out, size_t *out_len);
void platform_http_free(char *p);

/**
 * @brief Get the unique knob ID (MAC address based)
 * @param out Buffer to write the knob ID (13 bytes minimum: 12 hex + null)
 * @param len Buffer length
 */
void platform_http_get_knob_id(char *out, size_t len);
