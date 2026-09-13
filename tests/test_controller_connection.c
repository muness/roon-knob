
#include "controller_connection.h"
#include <assert.h>
#include <string.h>
static void test_evidence_states(void) {
    controller_connection_t c = {0}; char text[160];
    controller_connection_select(&c, "http://NAS2.local:8088", true, 1);
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "Searching"));
    c.discovered = true;
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "discovered") && strstr(text, "address"));
    c.resolved = true;
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "unavailable"));
    controller_connection_api(&c, true, 0, false, 100);
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "no playback zones"));
    controller_connection_api(&c, true, 2, false, 200);
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "choose a zone"));
    controller_connection_api(&c, true, 2, true, 300);
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "Ready"));
    controller_connection_expire(&c, 121000);
    assert(!controller_connection_ready(&c));
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "expired"));
    controller_connection_api(&c, true, 2, true, 300);
    controller_connection_api(&c, false, -1, false, 400);
    assert(c.last_success_ms == 300 && !c.reachable && !c.zones_current);
    controller_connection_summary(&c, text, sizeof(text));
    assert(!strstr(text, "Ready"));
    controller_connection_select(&c, "http://other:8088", false, 2);
    assert(!c.discovered && !c.resolved && !c.reachable && !c.last_success_ms);
    c.ambiguous = true;
    controller_connection_summary(&c, text, sizeof(text));
    assert(strstr(text, "Choose your bridge"));
}
static void test_bounded_retries(void) {
    controller_connection_t c = {0};
    controller_connection_select(&c, "http://NAS2.local:8088", true, 1);
    assert(controller_connection_due(&c, 100));
    controller_connection_schedule(&c, 100, false);
    assert(!controller_connection_due(&c, 101));
    for (unsigned i = 0; i < 100; ++i) {
        unsigned long long now = c.next_attempt_ms;
        controller_connection_schedule(&c, now, false);
        assert(c.next_attempt_ms > now && c.next_attempt_ms - now <= 60000);
    }
}
static void test_readable_details(void) {
 controller_connection_t c={0}; char text[512];
 controller_connection_select(&c,"",true,1);
 controller_connection_summary(&c,text,sizeof text);
 assert(strcmp(text,"Looking for a bridge")==0);
 c.resolved=true;c.resolver=CONNECTION_RESOLVER_LITERAL;strcpy(c.endpoint,"http://192.168.1.2:8088");
 controller_connection_api(&c,true,8,true,1000);
 controller_connection_details(&c,3000,text,sizeof text);
 assert(strstr(text,"Connection method: Automatic, using a saved address"));
 assert(strstr(text,"Playback zones: 8 available"));
 assert(strstr(text,"Last response: 2 seconds ago"));
 assert(!strstr(text,"not observed") && !strstr(text,"resolver:"));
}
int main(void) { test_readable_details(); test_evidence_states(); test_bounded_retries(); }
