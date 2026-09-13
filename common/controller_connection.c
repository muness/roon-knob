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
    if (c->offline) message = "Network unavailable";
    else if (c->stale) message = "Connection check pending - last result expired";
    else if (c->ambiguous) message = "Choose your bridge";
    else if (c->reachable && c->zones_current && c->zone_count == 0)
        message = "Bridge connected - no playback zones";
    else if (c->reachable && c->zones_current && !c->selected_zone_available)
        message = "Bridge connected - choose a zone";
    else if (controller_connection_ready(c))
        message = "Ready";
    else if (c->reachable) message = "Bridge connected - checking zones";
    else if (c->resolved) message = "Bridge unavailable - retrying automatically";
    else if (c->discovered) message = "Discovered - cannot resolve address; retrying";
    else if (c->automatic || !c->selected[0]) message = "Searching for selected bridge";
    else message = "Cannot resolve configured address - retrying";
    snprintf(out, len, "%s", message);
}
void controller_connection_details(const controller_connection_t *c, uint64_t now,
                                   char *out, size_t len) {
    const char *resolvers[] = {"none", "IP literal", "mDNS", "DNS"};
    uint64_t retry = now < c->next_attempt_ms ? (c->next_attempt_ms - now + 999)/1000 : 0;
    char success[40], zones[48];
    if (c->last_success_ms) snprintf(success, sizeof(success), "%llus ago", (unsigned long long)((now - c->last_success_ms)/1000));
    else snprintf(success, sizeof(success), "never");
    if (c->zones_current) snprintf(zones, sizeof(zones), "%d current", c->zone_count);
    else snprintf(zones, sizeof(zones), "unknown/stale");
    snprintf(out, len, "%s; discovered: %s; resolver: %s%s; address: %s; zones: %s; %s: %llus; last success: %s",
        c->automatic ? "Automatic" : "Manual", c->discovered ? "yes" : "not observed",
        resolvers[c->resolver], c->mdns_resolution_failed ? " (mDNS lookup failed)" : "",
        c->endpoint[0] ? c->endpoint : "unknown", zones,
        c->reachable ? "next check" : "retry", (unsigned long long)retry, success);
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
