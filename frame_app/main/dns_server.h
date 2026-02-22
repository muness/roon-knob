#ifndef DNS_SERVER_H
#define DNS_SERVER_H

// Start DNS server that redirects all queries to 192.168.4.1
// Call this when AP mode starts for captive portal detection
void dns_server_start(void);

// Stop DNS server
// Call this when AP mode stops
void dns_server_stop(void);

#endif // DNS_SERVER_H
