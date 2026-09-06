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
    test_rejects_missing_or_truncated_endpoint();
    return 0;
}
