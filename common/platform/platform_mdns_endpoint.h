#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DNS-SD identity is retained separately from its currently resolved address. */
typedef struct {
    char identity[128];
    char endpoint[128];
    bool seen, ambiguous;
} platform_mdns_observation_t;
/* Matching is scoped to the saved hostname, or an advertised address equal to
 * a legacy saved IP. Unrelated responders cannot replace a selection. */
void platform_mdns_consider_record(platform_mdns_observation_t *,
    const char *selected, const char *identity, const char *endpoint);
bool platform_mdns_url_host(const char *, char *, size_t, const char **);
