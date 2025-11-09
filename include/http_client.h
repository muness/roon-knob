#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int http_get(const char *url, char **out, size_t *out_len);
int http_post_json(const char *url, const char *json, char **out, size_t *out_len);
void http_free(char *p);

#ifdef __cplusplus
}
#endif
