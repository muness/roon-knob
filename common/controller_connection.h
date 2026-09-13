#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { CONNECTION_RESOLVER_NONE, CONNECTION_RESOLVER_LITERAL,
    CONNECTION_RESOLVER_MDNS, CONNECTION_RESOLVER_DNS } connection_resolver_t;
/* Owned by the bridge worker; readers receive a copy under its state lock. */
typedef struct {
    char selected[128];
    char endpoint[128];
    uint32_t generation;
    bool automatic, discovered, resolved, reachable, ambiguous, offline, stale;
    bool zones_current, selected_zone_available;
    int zone_count;
    connection_resolver_t resolver;
    bool mdns_resolution_failed;
    uint64_t observed_ms, last_success_ms, next_attempt_ms;
    unsigned failures;
} controller_connection_t;
void controller_connection_select(controller_connection_t *, const char *, bool, uint32_t);
void controller_connection_api(controller_connection_t *, bool, int, bool, uint64_t);
bool controller_connection_due(const controller_connection_t *, uint64_t);
void controller_connection_schedule(controller_connection_t *, uint64_t, bool);
void controller_connection_summary(const controller_connection_t *, char *, size_t);
void controller_connection_details(const controller_connection_t *, uint64_t, char *, size_t);

bool controller_connection_ready(const controller_connection_t *);

void controller_connection_expire(controller_connection_t *, uint64_t);
void controller_connection_recovery(const controller_connection_t *, const char *device_ip,
                                    char *title, size_t title_len, char *action, size_t action_len);
/* Derived attempt lifecycle; connection evidence remains the single owner of state. */
typedef enum {
    CONNECTION_WAIT_NETWORK,
    CONNECTION_WAIT_DISCOVERY_INIT,
    CONNECTION_WAIT_RETRY,
    CONNECTION_ATTEMPT_READY
} controller_connection_attempt_phase_t;
controller_connection_attempt_phase_t controller_connection_attempt_phase(
    const controller_connection_t *, bool needs_discovery, bool discovery_ready, uint64_t now);
