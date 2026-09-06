#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Prefer an address resolved from the mDNS service record over its optional
 * TXT base URL. TXT remains a compatibility fallback for responders that do
 * not return an address in the PTR result. */
bool platform_mdns_build_bridge_url(char *out, size_t len,
                                    const char *resolved_ipv4,
                                    uint16_t port,
                                    const char *txt_base);
