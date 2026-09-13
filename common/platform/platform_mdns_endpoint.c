#include "platform_mdns_endpoint.h"

#include <stdio.h>
#include <string.h>

bool platform_mdns_url_host(const char *url, char *host, size_t len, const char **suffix) {
    if (!url || strncmp(url, "http://", 7) || !host || !len) return false;
    const char *start = url + 7;
    size_t n = strcspn(start, ":/?#");
    if (!n || n >= len) return false;
    for (size_t i = 0; i < n; ++i) {
        char ch = start[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) return false;
        host[i] = ch >= 'A' && ch <= 'Z' ? (char)(ch + 'a' - 'A') : ch;
    }
    host[n] = 0;
    if (suffix) *suffix = start + n;
    return true;
}
static void normalize_local_host(char *host) {
    size_t n = strlen(host);
    if (n && host[n-1] == '.') host[--n] = 0;
    if (n > 6 && strcmp(host + n - 6, ".local") == 0) host[n-6] = 0;
}
static bool same_service(const char *a, const char *b) {
    char ah[64], bh[64]; const char *as, *bs;
    if (!platform_mdns_url_host(a, ah, sizeof(ah), &as) ||
        !platform_mdns_url_host(b, bh, sizeof(bh), &bs)) return false;
    normalize_local_host(ah); normalize_local_host(bh);
    return strcmp(ah, bh) == 0 && strcmp(as, bs) == 0;
}
void platform_mdns_consider_record(platform_mdns_observation_t *o,
    const char *selected, const char *identity, const char *endpoint) {
    if (!o || !selected || !identity || !identity[0] || !endpoint ||
        strlen(identity) >= sizeof(o->identity) || strlen(endpoint) >= sizeof(o->endpoint)) return;
    if (selected[0] && !same_service(selected, identity) &&
        !same_service(selected, endpoint)) return;
    if (o->seen && !same_service(o->identity, identity)) {
        o->ambiguous = true;
        return;
    }
    o->seen = true;
    snprintf(o->identity, sizeof(o->identity), "%s", identity);
    if (!o->endpoint[0] && endpoint[0])
        snprintf(o->endpoint, sizeof(o->endpoint), "%s", endpoint);
}
