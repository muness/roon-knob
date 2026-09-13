#include "platform/platform_mdns_endpoint.h"

#include <assert.h>
#include <string.h>

static void test_identity_selection(void) {
    platform_mdns_observation_t o = {0};
    platform_mdns_consider_record(&o, "http://NAS2:8088", "http://other.local:8088", "http://192.168.1.3:8088");
    assert(!o.seen);
    platform_mdns_consider_record(&o, "http://NAS2:8088", "http://nas2.local:8088", "");
    assert(o.seen && !o.endpoint[0]); /* discovered but unresolved */
    platform_mdns_consider_record(&o, "http://NAS2:8088", "http://nas2.local:8088", "http://192.168.1.4:8088");
    assert(strcmp(o.endpoint, "http://192.168.1.4:8088") == 0);
    o = (platform_mdns_observation_t){0};
    platform_mdns_consider_record(&o, "http://192.168.1.2:8088", "http://other.local:8088", "http://192.168.1.3:8088");
    assert(!o.seen);
    platform_mdns_consider_record(&o, "http://192.168.1.2:8088", "http://nas2.local:8088", "http://192.168.1.2:8088");
    assert(o.seen && strcmp(o.identity, "http://nas2.local:8088") == 0);
    o = (platform_mdns_observation_t){0};
    platform_mdns_consider_record(&o, "", "http://nas2.local:8088", "http://192.168.1.2:8088");
    platform_mdns_consider_record(&o, "", "http://other.local:8088", "http://192.168.1.3:8088");
    assert(o.ambiguous);
    /* The same installation repeated across interfaces is not another choice. */
    o = (platform_mdns_observation_t){0};
    platform_mdns_consider_record(&o, "", "http://nas2.local:8088", "");
    platform_mdns_consider_record(&o, "", "http://nas2.local:8088", "http://192.168.1.2:8088");
    assert(!o.ambiguous && o.endpoint[0]);
}
int main(void) { test_identity_selection(); return 0; }
