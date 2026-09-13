#include "controller_connection.h"
#include <stdio.h>
#include <string.h>

void controller_connection_select(controller_connection_t *c, const char *url,
                                  bool automatic, uint32_t generation) {
    if (c->generation == generation && c->automatic == automatic &&
        strcmp(c->selected, url) == 0) return;
    memset(c, 0, sizeof(*c));
    snprintf(c->selected, sizeof(c->selected), "%s", url);
    c->automatic = automatic;
    c->generation = generation;
    c->zone_count = -1;
}
void controller_connection_api(controller_connection_t *c, bool valid, int zones,
                              bool selected, uint64_t now) {
    c->observed_ms = now;
    c->stale = false;
    c->reachable = valid;
    c->zones_current = valid && zones >= 0;
    c->selected_zone_available = valid && selected;
    if (valid) {
        c->last_success_ms = now;
        if (zones >= 0) c->zone_count = zones;
    }
}
bool controller_connection_due(const controller_connection_t *c, uint64_t now) {
    return now >= c->next_attempt_ms;
}
void controller_connection_schedule(controller_connection_t *c, uint64_t now, bool ok) {
    static const unsigned delays[] = {5000, 15000, 30000, 60000};
    if (ok) { c->failures = 0; c->next_attempt_ms = now + 60000; }
    else {
        unsigned n = c->failures < 4 ? c->failures++ : 3;
        c->next_attempt_ms = now + delays[n];
    }
}
void controller_connection_summary(const controller_connection_t *c, char *out, size_t len) {
    const char *message;
    if (c->offline) message = "Wi-Fi disconnected - reconnecting automatically";
    else if (c->stale) message = "Connection status expired - checking again";
    else if (c->ambiguous) message = "Choose your bridge";
    else if (c->reachable && c->zones_current && c->zone_count == 0)
        message = "Bridge connected - no playback zones; check your music server";
    else if (c->reachable && c->zones_current && !c->selected_zone_available)
        message = "Bridge connected - choose a zone";
    else if (controller_connection_ready(c))
        message = "Ready";
    else if (c->reachable) message = "Bridge connected - checking zones";
    else if (c->resolved) message = "Bridge unavailable - retrying automatically";
    else if (c->discovered) message = "Bridge discovered, but its address is unavailable - retrying";
    else if (!c->selected[0]) message = "Looking for a bridge";
    else if (c->automatic) message = "Searching for saved bridge";
    else message = "Cannot find bridge address - retrying automatically";
    snprintf(out, len, "%s", message);
}
void controller_connection_details(const controller_connection_t *c, uint64_t now,
                                   char *out, size_t len) {
    uint64_t retry = now < c->next_attempt_ms ? (c->next_attempt_ms - now + 999)/1000 : 0;
    char success[48], zones[48], next[64];
    if (c->last_success_ms && now >= c->last_success_ms)
        snprintf(success, sizeof(success), "%llu seconds ago", (unsigned long long)((now - c->last_success_ms)/1000));
    else snprintf(success, sizeof(success), "Not yet received");
    if (c->zones_current) snprintf(zones, sizeof(zones), "%d available", c->zone_count);
    else snprintf(zones, sizeof(zones), "Waiting for an update");
    if (retry) snprintf(next, sizeof(next), "In %llu seconds", (unsigned long long)retry);
    else snprintf(next, sizeof(next), "Due now");
    const char *method = c->automatic && !c->selected[0] && !c->discovered && !c->resolved ? "Automatic discovery" : !c->automatic ? "Manually configured" :
        c->resolver == CONNECTION_RESOLVER_LITERAL ? "Automatic, using a saved address" :
        c->discovered ? "Automatic discovery" : "Automatic, using a saved bridge";
    snprintf(out, len, "Bridge address: %s\nConnection method: %s\nPlayback zones: %s\nLast response: %s\n%s: %s",
        c->endpoint[0] ? c->endpoint : "Not found yet", method, zones, success,
        c->reachable ? "Next connection check" : "Next retry", next);
}

bool controller_connection_ready(const controller_connection_t *c) {
    return !c->offline && !c->stale && !c->ambiguous && c->reachable && c->zones_current &&
           c->zone_count > 0 && c->selected_zone_available;
}

void controller_connection_expire(controller_connection_t *c, uint64_t now) {
    if (c->last_success_ms && now > c->last_success_ms && now - c->last_success_ms > 120000) {
        c->stale = true;
        c->reachable = false;
        c->zones_current = false;
    }
}

void controller_connection_recovery(const controller_connection_t *c, const char *device_ip,
                                    char *title, size_t title_len, char *action, size_t action_len) {
    const char *message = c->offline ? "Wi-Fi disconnected" :
        c->ambiguous ? "Choose a bridge" :
        c->reachable ? (c->zones_current ?
            (c->zone_count == 0 ? "No playback zones" : "Choose a zone") : "Checking zones...") :
        c->discovered && !c->resolved ? "Bridge address missing" :
        c->resolved ? "Bridge unavailable" :
        c->failures ? "Bridge not found" : "Finding bridge...";
    snprintf(title, title_len, "%s", message);
    if (c->offline || !device_ip || !device_ip[0])
        snprintf(action, action_len, "Open Settings");
    else if (c->reachable)
        snprintf(action, action_len, "Check Hi-Fi Control");
    else
        snprintf(action, action_len, "Manual setup:\nhttp://%s", device_ip);
}

controller_connection_attempt_phase_t controller_connection_attempt_phase(
    const controller_connection_t *c, bool needs_discovery, bool discovery_ready, uint64_t now) {
    if (c->offline) return CONNECTION_WAIT_NETWORK;
    if (needs_discovery && !discovery_ready) return CONNECTION_WAIT_DISCOVERY_INIT;
    if (!controller_connection_due(c, now)) return CONNECTION_WAIT_RETRY;
    return CONNECTION_ATTEMPT_READY;
}
