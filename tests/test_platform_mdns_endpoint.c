#include "platform/platform_mdns_endpoint.h"

#include <assert.h>
#include <string.h>

static void test_resolved_ipv4_wins_over_txt_hostname(void) {
    char url[64];

    assert(platform_mdns_build_bridge_url(
        url, sizeof(url), "192.168.1.42", 8088,
        "http://docker-container-name:8088"));
    assert(strcmp(url, "http://192.168.1.42:8088") == 0);
}

static void test_txt_is_compatibility_fallback(void) {
    char url[64];

    assert(platform_mdns_build_bridge_url(
        url, sizeof(url), NULL, 0, "http://bridge.local:8088"));
    assert(strcmp(url, "http://bridge.local:8088") == 0);
}

static void test_txt_fallback_waits_for_later_resolved_ipv4(void) {
    char selected[64] = {0};
    char fallback[64] = {0};

    assert(!platform_mdns_consider_bridge_url(
        selected, sizeof(selected), fallback, sizeof(fallback), NULL, 0,
        "http://first-result.local:8088"));
    assert(strcmp(fallback, "http://first-result.local:8088") == 0);
    assert(platform_mdns_consider_bridge_url(
        selected, sizeof(selected), fallback, sizeof(fallback), "192.168.1.42",
        8088, NULL));
    assert(strcmp(selected, "http://192.168.1.42:8088") == 0);
    assert(strcmp(fallback, "http://first-result.local:8088") == 0);
}

static void test_first_txt_fallback_is_retained(void) {
    char selected[64] = {0};
    char fallback[64] = {0};

    assert(!platform_mdns_consider_bridge_url(
        selected, sizeof(selected), fallback, sizeof(fallback), NULL, 0,
        "http://first-result.local:8088"));
    assert(!platform_mdns_consider_bridge_url(
        selected, sizeof(selected), fallback, sizeof(fallback), NULL, 0,
        "http://second-result.local:8088"));
    assert(strcmp(fallback, "http://first-result.local:8088") == 0);
}

static void test_rejects_missing_or_truncated_endpoint(void) {
    char url[16];

    assert(!platform_mdns_build_bridge_url(
        url, sizeof(url), NULL, 0, NULL));
    assert(!platform_mdns_build_bridge_url(
        url, sizeof(url), "192.168.1.42", 8088, NULL));
    assert(!platform_mdns_build_bridge_url(
        url, sizeof(url), "192.168.1.42", 8088,
        "http://docker-container-name:8088"));
}


static int discover_calls, resolve_calls, verify_calls;
static const char *response;
static bool resolution_ok, verification_ok;
static bool discover_stub(char *out, size_t len) {
    ++discover_calls;
    assert(strlen(response) < len);
    strcpy(out, response);
    return true;
}
static bool resolve_stub(const char *host, char *out, size_t len) {
    ++resolve_calls;
    assert(strcmp(host, "da3543ad-unified-hifi-control") == 0);
    assert(len >= 16);
    strcpy(out, "192.168.68.10");
    return resolution_ok;
}
static bool verify_stub(const char *url) {
    ++verify_calls;
    assert(strcmp(url, "http://192.168.68.10:8088") == 0);
    return verification_ok;
}
static void test_saved_ip_is_never_replaced(const char *advertised) {
    char out[128];
    response = advertised;
    discover_calls = resolve_calls = verify_calls = 0;
    assert(!platform_mdns_select_bridge_update(
        "http://192.168.68.10:8088", true, out, sizeof(out),
        discover_stub, resolve_stub, verify_stub));
    assert(!out[0]);
    assert(discover_calls == 0 && resolve_calls == 0 && verify_calls == 0);
}
static void test_legacy_hostname_requires_resolution_and_verification(void) {
    char out[128];
    response = "http://192.168.68.99:8088"; /* another responder */
    discover_calls = resolve_calls = verify_calls = 0;
    resolution_ok = false;
    verification_ok = true;
    assert(!platform_mdns_select_bridge_update(
        "http://da3543ad-unified-hifi-control:8088", true, out, sizeof(out),
        discover_stub, resolve_stub, verify_stub));
    assert(!out[0] && discover_calls == 0 && verify_calls == 0);
    resolution_ok = true;
    verification_ok = false;
    assert(!platform_mdns_select_bridge_update(
        "http://da3543ad-unified-hifi-control:8088", true, out, sizeof(out),
        discover_stub, resolve_stub, verify_stub));
    assert(!out[0] && discover_calls == 0 && verify_calls == 1);
    verification_ok = true;
    assert(platform_mdns_select_bridge_update(
        "http://da3543ad-unified-hifi-control:8088", true, out, sizeof(out),
        discover_stub, resolve_stub, verify_stub));
    assert(strcmp(out, "http://192.168.68.10:8088") == 0);
    assert(discover_calls == 0 && verify_calls == 2);
    assert(!platform_mdns_select_bridge_update(
        "http://da3543ad-unified-hifi-control:8088", false, out, sizeof(out),
        discover_stub, resolve_stub, verify_stub));
    assert(verify_calls == 2); /* manual URLs remain authoritative */
    assert(platform_mdns_select_bridge_update(
        "", false, out, sizeof(out), discover_stub, resolve_stub, verify_stub));
    assert(discover_calls == 1); /* explicit Clear permits a new bridge */
}
int main(void) {
    test_saved_ip_is_never_replaced("http://da3543ad-unified-hifi-control:8088");
    test_saved_ip_is_never_replaced("http://192.168.68.99:8088");
    test_legacy_hostname_requires_resolution_and_verification();
    test_resolved_ipv4_wins_over_txt_hostname();
    test_txt_is_compatibility_fallback();
    test_txt_fallback_waits_for_later_resolved_ipv4();
    test_first_txt_fallback_is_retained();
    test_rejects_missing_or_truncated_endpoint();
    return 0;
}
