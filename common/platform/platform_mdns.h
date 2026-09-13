#pragma once

#include <stdbool.h>
#include <stddef.h>

void platform_mdns_init(const char *hostname);
bool platform_mdns_is_ready(void);

// Resolve a .local hostname to IP address via mDNS
// hostname can be "foo" or "foo.local" - .local suffix is stripped automatically
bool platform_mdns_resolve_local(const char *hostname, char *ip_out, size_t ip_len);

#include "platform_mdns_endpoint.h"
void platform_mdns_observe_bridge(const char *selected, platform_mdns_observation_t *out);
/* Resolve the selected name without substituting a different service. */
bool platform_mdns_resolve_base_url(const char *base, char *out, size_t len,
                                    bool *used_dns, bool *mdns_failed);
