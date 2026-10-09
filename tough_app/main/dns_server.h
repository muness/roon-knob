#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <stdbool.h>

// Start DNS server that redirects all queries to 192.168.4.1
// Call this when AP mode starts for captive portal detection
// Returns true only after the DNS socket and serving task are active.
bool dns_server_start(void);

// Stop DNS server
// Call this when AP mode stops
void dns_server_stop(void);

// True only while the captive-DNS service is active.
bool dns_server_is_running(void);

#endif // DNS_SERVER_H
