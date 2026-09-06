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

int main(void) {
    test_resolved_ipv4_wins_over_txt_hostname();
    test_txt_is_compatibility_fallback();
    test_txt_fallback_waits_for_later_resolved_ipv4();
    test_first_txt_fallback_is_retained();
    test_rejects_missing_or_truncated_endpoint();
    return 0;
}
