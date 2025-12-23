#pragma once

#include <stdbool.h>
#include <stddef.h>

void platform_mdns_init(const char *hostname);
bool platform_mdns_discover_base_url(char *out, size_t len);

// Resolve a .local hostname to IP address via mDNS
// hostname can be "foo" or "foo.local" - .local suffix is stripped automatically
bool platform_mdns_resolve_local(const char *hostname, char *ip_out, size_t ip_len);
