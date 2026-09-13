/* Execute the real bridge recovery path with deterministic network/config I/O.
 * Unused playback sections are discarded by the linker. */
#include "../common/bridge_client.c"
#include <assert.h>
static controller_config_snapshot_t fixture;
static platform_mdns_observation_t advertised;
static uint64_t clock_ms;
static uint32_t generation;
static unsigned discovery_calls, resolve_calls, http_calls, writes;
static bool resolution_ok, http_ok, race;
static const char *http_body;
static void (*queued)(void *);
static void *queued_arg;
bool controller_config_snapshot(controller_config_snapshot_t *out) { *out = fixture; return true; }
bool controller_config_capture_endpoint_token(controller_config_endpoint_token_t *out) {
    memset(out, 0, sizeof(*out)); out->generation = generation;
    strcpy(out->bridge_base, fixture.value.bridge_base);
    out->from_mdns = fixture.value.bridge_from_mdns;
    return true;
}
controller_config_write_result_t controller_config_set_endpoint_if_current(
    const controller_config_endpoint_token_t *token, const char *base,
    bool from_mdns, bool empty, controller_config_snapshot_t *out) {
    if (token->generation != generation || (empty && fixture.value.bridge_base[0]))
        return CONTROLLER_CONFIG_NOT_COMMITTED;
    strcpy(fixture.value.bridge_base, base); fixture.value.bridge_from_mdns = from_mdns;
    ++generation; ++writes; *out = fixture;
    return CONTROLLER_CONFIG_COMMITTED_VERIFIED;
}
void platform_mdns_observe_bridge(const char *selected, platform_mdns_observation_t *out) {
    (void)selected; ++discovery_calls; *out = advertised;
}
bool platform_mdns_resolve_base_url(const char *base, char *out, size_t len,
                                    bool *dns, bool *failed) {
    (void)base; (void)len; ++resolve_calls; *dns = false; *failed = !resolution_ok;
    strcpy(out, resolution_ok ? "http://192.168.1.2:8088" : "");
    return resolution_ok;
}
uint64_t platform_millis(void) { return clock_ms; }
int platform_http_get(const char *url, char **out, size_t *len) {
    assert(strstr(url, "/zones?knob_id=")); ++http_calls;
    if (race) { ++generation; strcpy(fixture.value.bridge_base, "http://manual:8088"); fixture.value.bridge_from_mdns = false; }
    *out = http_ok ? strdup(http_body) : NULL;
    *len = *out ? strlen(*out) : 0;
    return http_ok ? 0 : -1;
}
void platform_http_get_knob_id(char *out, size_t len) { (void)len; strcpy(out,"abc"); }
void platform_http_free(char *value) { free(value); }
bool platform_task_post_to_ui(platform_task_fn_t cb, void *arg) {
    /* Only endpoint commits are delayed deliberately for race tests. Other
     * callbacks must consume their owned messages as the real UI loop does. */
    if (cb == commit_discovered_endpoint_on_ui) {
        assert(!queued);
        queued = cb; queued_arg = arg;
    } else {
        cb(arg);
    }
    return true;
}
void controller_presentation_set_message(const char *msg) { (void)msg; }
void controller_presentation_set_network_status(const char *msg) { (void)msg; }
void platform_log_backend(const char *level, const char *fmt, va_list args) { (void)level; (void)fmt; (void)args; }
controller_config_write_result_t controller_config_set_zone(const char *zone, controller_config_snapshot_t *out) {
    strcpy(fixture.value.zone_id, zone); *out = fixture;
    return CONTROLLER_CONFIG_COMMITTED_VERIFIED;
}
void controller_presentation_set_zone_name(const char *name) { (void)name; }
static unsigned controls;
int platform_http_post_json(const char *url, const char *body, char **out, size_t *len) {
    assert(strstr(url, "/control")); assert(strstr(body, "vol_abs"));
    ++controls; *out = strdup("{}"); *len = 2; return 0;
}
void controller_presentation_show_volume_change(float value, float step) { (void)value; (void)step; }
static void reset(const char *url, bool automatic) {
    memset(&fixture, 0, sizeof(fixture)); memset(&s_connection,0,sizeof(s_connection));
    memset(&s_state,0,sizeof(s_state)); advertised = (platform_mdns_observation_t){0};
    strcpy(fixture.value.bridge_base,url); fixture.value.bridge_from_mdns = automatic;
    generation = 1; clock_ms = 1000; discovery_calls = resolve_calls = http_calls = writes = 0;
    queued = NULL; resolution_ok = http_ok = true; race = false;
    http_body = "{\"zones\":[]}";
    atomic_store(&s_discovered_endpoint_commit_pending, false);
    atomic_store(&s_network_ready, true);
}
static void test_unresolved_then_recovered(void) {
    reset("http://NAS2:8088", true);
    advertised.seen = true; strcpy(advertised.identity, "http://nas2.local:8088");
    resolution_ok = false;
    update_connection();
    assert(s_connection.discovered && !s_connection.resolved && !s_connection.reachable);
    assert(writes == 0 && !queued && http_calls == 0);
    for (unsigned i=0; i<100; ++i) update_connection();
    assert(discovery_calls == 1); /* polling cannot bypass backoff */
    clock_ms = s_connection.next_attempt_ms; resolution_ok = true;
    update_connection();
    assert(s_connection.reachable && s_connection.zone_count == 0 && queued);
    { void (*cb)(void *) = queued; void *arg = queued_arg; queued = NULL; queued_arg = NULL; cb(arg); } assert(writes == 1);
    assert(strcmp(fixture.value.bridge_base,"http://nas2.local:8088") == 0);
    /* After restart and DHCP change, resolve the durable name to its new address. */
    memset(&s_connection,0,sizeof(s_connection));
    strcpy(advertised.endpoint,"http://192.168.1.44:8088");
    update_connection();
    assert(strcmp(s_connection.endpoint,"http://192.168.1.44:8088") == 0 && writes == 1);
}
static void test_failed_and_stale_candidates(void) {
    reset("http://NAS2:8088",true);
    advertised.seen = true; strcpy(advertised.identity,"http://nas2.local:8088");
    strcpy(advertised.endpoint,"http://192.168.1.2:8088");
    http_body = "{\"error\":\"not a bridge\"}";
    update_connection(); assert(!queued && !s_connection.reachable && writes == 0);
    clock_ms = s_connection.next_attempt_ms; http_body = "{\"zones\":[]}"; race = true;
    update_connection(); assert(!queued && writes == 0);
    controller_connection_t out; bridge_client_connection_snapshot(&out);
    assert(!out.reachable && strcmp(out.selected,"http://manual:8088") == 0);
    reset("http://NAS2:8088",true);
    advertised.seen = true; strcpy(advertised.identity,"http://nas2.local:8088");
    strcpy(advertised.endpoint,"http://192.168.1.2:8088");
    update_connection(); assert(queued);
    ++generation; strcpy(fixture.value.bridge_base,"http://manual:8088");
    fixture.value.bridge_from_mdns = false;
    { void (*cb)(void *) = queued; void *arg = queued_arg; queued = NULL; queued_arg = NULL; cb(arg); } assert(writes == 0);
    reset("http://NAS2:8088",false);
    update_connection(); assert(discovery_calls == 0 && writes == 0);
}
static void test_volume_uses_current_connection_evidence(void) {
    reset("http://192.168.1.2:8088", true);
    strcpy(fixture.value.zone_id,"roon:room");
    controller_connection_select(&s_connection, fixture.value.bridge_base, true, generation);
    s_connection.resolved = true; strcpy(s_connection.endpoint, fixture.value.bridge_base);
    controller_connection_api(&s_connection, true, 1, true, clock_ms);
    s_last_known_volume = 30; s_last_known_volume_min = 0; s_last_known_volume_max = 100; s_last_known_volume_step = 1;
    controller_command_t command = {.kind = CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, .volume_steps = 1};
    controls = 0;
    assert(bridge_client_execute_command(&command));
    assert(controls == 1);
}
static void test_zone_refresh_invalidates_old_readiness(void) {
    reset("http://192.168.1.2:8088", true);
    strcpy(fixture.value.zone_id, "roon:room");
    controller_connection_api(&s_connection, true, 1, true, clock_ms);
    parse_zones_from_response("{\"zones\":[]}");
    assert(!controller_connection_ready(&s_connection));
    assert(s_connection.reachable && s_connection.zone_count == 0);
    parse_zones_from_response("{\"error\":\"unavailable\"}");
    assert(!s_connection.reachable && !s_connection.zones_current);
}
static void test_partial_api_success_preserves_backoff(void) {
    reset("http://192.168.1.2:8088", false);
    const unsigned delays[] = {5000, 15000, 30000, 60000, 60000};
    for (unsigned i = 0; i < 5; ++i) {
        update_connection();
        assert(s_connection.reachable);
        controller_connection_api(&s_connection, false, -1, false, clock_ms);
        controller_connection_schedule(&s_connection, clock_ms, false);
        assert(s_connection.next_attempt_ms - clock_ms == delays[i]);
        clock_ms = s_connection.next_attempt_ms;
    }
}
static void test_zone_cycle_wraps_without_copying_inventory(void) {
    reset("http://192.168.1.2:8088", true);
    s_state.zone_count = 2;
    strcpy(s_state.zones[0].id, "roon:a"); strcpy(s_state.zones[0].name, "A");
    strcpy(s_state.zones[1].id, "roon:b"); strcpy(s_state.zones[1].name, "B");
    strcpy(s_state.runtime_zone_id, "roon:a"); s_state.runtime_zone_pinned = true;
    controller_command_t command = {.kind = CONTROLLER_COMMAND_PREVIOUS_ZONE};
    assert(bridge_client_execute_command(&command));
    assert(strcmp(s_state.runtime_zone_id, "roon:b") == 0);
    command.kind = CONTROLLER_COMMAND_NEXT_ZONE;
    assert(bridge_client_execute_command(&command));
    assert(strcmp(s_state.runtime_zone_id, "roon:a") == 0);
}
int main(void) { test_zone_cycle_wraps_without_copying_inventory(); test_partial_api_success_preserves_backoff(); test_zone_refresh_invalidates_old_readiness(); test_volume_uses_current_connection_evidence(); test_unresolved_then_recovered(); test_failed_and_stale_candidates(); }
