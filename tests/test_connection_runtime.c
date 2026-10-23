/* Execute the real bridge recovery path with deterministic network/config I/O.
 * Unused playback sections are discarded by the linker. */
#include <stdlib.h>
static int bridge_alloc_fail_after = -1;
static void *test_bridge_alloc(size_t size);
#define malloc test_bridge_alloc
#include "../common/bridge_client.c"
#undef malloc
static void *test_bridge_alloc(size_t size) {
    if (bridge_alloc_fail_after == 0) return NULL;
    if (bridge_alloc_fail_after > 0) --bridge_alloc_fail_after;
    return malloc(size);
}
#include <assert.h>
static controller_config_snapshot_t fixture;
static platform_mdns_observation_t advertised;
static uint64_t clock_ms;
static uint32_t generation;
static unsigned discovery_calls, resolve_calls, http_calls, writes;
static bool mdns_ready = true;
bool platform_mdns_is_ready(void) { return mdns_ready; }
static bool resolution_ok, http_ok, race;
static const char *http_body;
static platform_bridge_cache_t retained_cache;
static char network_context[96] = "home|192.168.1.1|255.255.255.0";
static bool disconnect_during_http;
static int response_race;
static unsigned media_updates, zone_name_updates, zone_count_updates;
void controller_view_compat_apply_media(const controller_media_view_t *view) { (void)view; ++media_updates; }
bool wifi_mgr_get_network_context(char *out, size_t len) {
    snprintf(out, len, "%s", network_context); return out[0] != 0;
}
bool platform_bridge_cache_read(platform_bridge_cache_t *out) {
    *out = retained_cache; return out->version == 1;
}
void platform_bridge_cache_write(const platform_bridge_cache_t *value) { retained_cache = *value; }

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
    assert(strstr(url, "/zones?knob_id=") || strstr(url, "/now_playing?zone_id=")); ++http_calls;
    if (disconnect_during_http) {
        atomic_store(&s_network_ready, false);
        atomic_fetch_add(&s_network_epoch, 1);
    }
    if (response_race == 1) ++fixture.revision; // Includes A->B->A config transitions.
    if (response_race == 2) { pin_runtime_zone_selection("other"); pin_runtime_zone_selection("roon:room"); }
    if (response_race == 3) atomic_fetch_add(&s_network_epoch, 1);
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
    strcpy(fixture.value.zone_id, zone); ++fixture.revision; *out = fixture;
    return CONTROLLER_CONFIG_COMMITTED_VERIFIED;
}
void controller_presentation_set_zone_name(const char *name) { (void)name; ++zone_name_updates; }
void controller_presentation_set_zone_count(int count) { (void)count; ++zone_count_updates; }
static unsigned controls;
int platform_http_post_json(const char *url, const char *body, char **out, size_t *len) {
    assert(strstr(url, "/control")); assert(strstr(body, "vol_abs"));
    ++controls; *out = strdup("{}"); *len = 2; return 0;
}
static unsigned volume_overlays;
void controller_presentation_show_volume_change(float value, float step) { (void)value; (void)step; ++volume_overlays; }
static void reset(const char *url, bool automatic) {
    response_race = 0;
    free(s_artwork_identity); s_artwork_identity = NULL;
    s_selection_generation = s_media_generation = s_inventory_generation = 0;
    memset(&s_volume_context, 0, sizeof(s_volume_context));
    s_media_last_good_ms = 0; s_response_failure = RESPONSE_OK;
    memset(&fixture, 0, sizeof(fixture)); memset(&s_connection,0,sizeof(s_connection));
    memset(&s_state,0,sizeof(s_state)); advertised = (platform_mdns_observation_t){0};
    strcpy(fixture.value.bridge_base,url); fixture.value.bridge_from_mdns = automatic;
    memset(&retained_cache, 0, sizeof(retained_cache));
    s_connection_network[0] = 0; disconnect_during_http = false;
    strcpy(network_context, "home|192.168.1.1|255.255.255.0");
    generation = 1; clock_ms = 1000; discovery_calls = resolve_calls = http_calls = writes = 0;
    queued = NULL; resolution_ok = http_ok = true; race = false;
    s_last_known_volume_fixed = true; s_volume_capability_zone[0] = 0; controls=volume_overlays=0;
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
    s_last_known_volume_fixed=false; strcpy(s_volume_capability_zone,"roon:room");
    strcpy(s_volume_capability_bridge,fixture.value.bridge_base); s_volume_capability_generation=generation;
    assert(capture_response_context(&s_volume_context)); s_media_last_good_ms = clock_ms;
    assert(bridge_client_execute_command(&command));
    assert(controls == 1);
}
static void test_fixed_volume_response_and_zone_switch(void) {
    test_volume_uses_current_connection_evidence();
    struct now_playing_state state = {0};
    platform_power_snapshot_t power = {.battery_level = -1};
    controller_command_t command = controller_command_adjust_volume(3);
    controls=volume_overlays=0;
    http_body="{\"volume_type\":\"fixed\",\"volume\":40,\"volume_min\":0,\"volume_max\":100}";
    assert(fetch_now_playing(&state,&power));
    float prior = s_last_known_volume;
    assert(bridge_client_execute_command(&command));
    assert(controls==0 && volume_overlays==0 && s_last_known_volume==prior);
    const char *types[]={"db","number"};
    for(size_t i=0;i<2;i++) {
        char body[160]; snprintf(body,sizeof(body),"{\"volume_type\":\"%s\",\"volume\":40,\"volume_min\":0,\"volume_max\":100,\"volume_step\":1}",types[i]); http_body=body;
        assert(fetch_now_playing(&state,&power));
        assert(bridge_client_execute_command(&command));
        assert(controls==i+1 && volume_overlays==i+1);
    }
    strcpy(fixture.value.zone_id,"roon:other");
    assert(bridge_client_execute_command(&command)); /* prior-zone capability cannot authorize */
    assert(controls==2 && volume_overlays==2);
    const char *unknown[]={"{}","{\"volume_type\":null}","{\"volume_type\":\"surprise\"}",
        "{\"line1\":\"volume_type db\",\"metadata\":{\"volume_type\":\"db\"}}"};
    for(size_t i=0;i<4;i++) {
        http_body=unknown[i]; assert(fetch_now_playing(&state,&power) == (i >= 2));
        assert(bridge_client_execute_command(&command)); assert(controls==2 && volume_overlays==2);
    }
    http_body="{\"volume_type\":\"number\",\"volume\":40,\"volume_min\":0,\"volume_max\":100,\"volume_step\":1}"; assert(fetch_now_playing(&state,&power));
    assert(bridge_client_execute_command(&command)); assert(controls==3 && volume_overlays==3);
    ++generation; strcpy(fixture.value.bridge_base,"http://newbridge:8088");
    assert(bridge_client_execute_command(&command)); assert(controls==3 && volume_overlays==3);
    assert(fetch_now_playing(&state,&power));
    controller_connection_select(&s_connection,fixture.value.bridge_base,fixture.value.bridge_from_mdns,generation);
    s_connection.resolved=true; strcpy(s_connection.endpoint,fixture.value.bridge_base);
    controller_connection_api(&s_connection,true,1,true,clock_ms);
    assert(bridge_client_execute_command(&command)); assert(controls==4 && volume_overlays==4);
    /* A response issued before a manual endpoint change carries the old token. */
    race=true; assert(!fetch_now_playing(&state,&power)); race=false;
    assert(bridge_client_execute_command(&command)); assert(controls==4 && volume_overlays==4);
    http_body="{\"volume_type\":\"fixed\"}"; assert(fetch_now_playing(&state,&power));
    assert(bridge_client_execute_command(&command)); assert(controls==4 && volume_overlays==4);
}
static void test_now_playing_json_strings(void) {
    reset("http://192.168.1.2:8088",false); strcpy(fixture.value.zone_id,"roon:room");
    struct now_playing_state state={0}; platform_power_snapshot_t power={.battery_level=-1};
    http_body="{\"line1\":\"Maryama \\\"Terre battue\\\"\",\"line2\":\"Lee \\\"Scratch\\\" Perry\",\"line3\":\"C:\\\\Music \\u00e9 \\uD83C\\uDFB5\"}";
    assert(fetch_now_playing(&state,&power));
    assert(!strcmp(state.line1,"Maryama \"Terre battue\""));
    assert(!strcmp(state.line2,"Lee \"Scratch\" Perry"));
    assert(!strcmp(state.line3,"C:\\Music é 🎵"));
    http_body="{\"line1\":null,\"line2\":false,\"line3\":42,\"image_key\":null,\"config_sha\":\"next\"}";
    strcpy(state.image_key,"prior"); assert(fetch_now_playing(&state,&power));
    assert(!state.line1[0] && !state.image_key[0]);
    assert(!state.line2[0] && !state.line3[0]);
    http_body="{}"; assert(!fetch_now_playing(&state,&power)); assert(!state.line1[0]);
    char bounded[4]="old";
    cJSON *root = cJSON_Parse("{\"line1\":\"éé\",\"line2\":\"later\"}");
    assert(root);
    json_string_field(root, "line1", bounded, sizeof(bounded));
    assert(!strcmp(bounded,"é"));
    json_string_field(root, "line1", bounded, 0);
    assert(!strcmp(bounded,"é"));
    cJSON_Delete(root);
}
static char *playback_fixture(const char *name) {
    char path[160];
    snprintf(path, sizeof(path), "tests/fixtures/bridge_playback/%s.json", name);
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    char *body = malloc((size_t)size + 1);
    assert(body && fread(body, 1, (size_t)size, file) == (size_t)size);
    body[size] = 0;
    fclose(file);
    return body;
}
static void test_idle_zone_coexists_with_playing_fixed_volume_zone(void) {
    reset("http://192.168.1.2:8088", false);
    const char *selected = "roon:1601bc1d89689dfb2b8de5ea21040d4741d6";
    strcpy(fixture.value.zone_id, selected);
    char *zones = playback_fixture("zones");
    http_body = zones;
    assert(update_connection() && refresh_zone_label(true));
    assert(controller_connection_ready(&s_connection) && s_state.zone_count == 2);
    assert(!strcmp(s_state.runtime_zone_id, selected));
    assert(!strcmp(s_state.zone_label, "Keltons Gustard"));
    /* Order, and the unrelated stopped zone disappearing, cannot change selection. */
    cJSON *root = cJSON_Parse(zones);
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "zones");
    cJSON *idle = cJSON_DetachItemFromArray(list, 0);
    cJSON_AddItemToArray(list, idle);
    char *reordered = cJSON_PrintUnformatted(root);
    http_body = reordered;
    assert(refresh_zone_label(true));
    assert(!strcmp(s_state.runtime_zone_id, selected));
    cJSON_DeleteItemFromArray(list, 1);
    char *playing_only = cJSON_PrintUnformatted(root);
    http_body = playing_only;
    assert(refresh_zone_label(true) && controller_connection_ready(&s_connection));
    cJSON_Delete(root); free(reordered); free(playing_only); free(zones);

    struct now_playing_state state = {0};
    default_now_playing(&state);
    platform_power_snapshot_t power = {.battery_level = -1};
    char *playing = playback_fixture("playing");
    http_body = playing;
    for (int i = 0; i < 20; ++i) {
        assert(fetch_now_playing(&state, &power));
        assert(state.is_playing && !strcmp(state.line1, "Breadwinner"));
        assert(state.image_key[0]);
        assert(state.seek_position == 122 && state.length == 173);
        assert(s_last_known_volume_fixed);
        controller_command_t command = controller_command_adjust_volume(1);
        assert(bridge_client_execute_command(&command) && controls == 0);
    }
    free(playing);
    /* Selecting a genuinely idle zone must clear old artwork and progress. */
    strcpy(fixture.value.zone_id, "roon:1601c43d3e2aad4b4d38b7baf9f967806de4");
    s_state.runtime_zone_pinned = false;
    char *stopped = playback_fixture("idle");
    http_body = stopped;
    assert(fetch_now_playing(&state, &power));
    assert(!state.is_playing && !strcmp(state.line1, "Idle"));
    assert(!state.line2[0] && !state.line3[0] && !state.image_key[0]);
    assert(!state.seek_position && !state.length && s_last_known_volume_fixed);
    free(stopped);
}
static void test_playback_metadata_cannot_report_a_connection_error(void) {
    reset("http://192.168.1.2:8088", false);
    strcpy(fixture.value.zone_id, "roon:room");
    struct now_playing_state state = {0};
    default_now_playing(&state);
    platform_power_snapshot_t power = {.battery_level = -1};
    http_body = "{\"line1\":\"error\",\"is_playing\":true,\"image_key\":\"cover\"}";
    assert(fetch_now_playing(&state, &power));
    assert(state.is_playing && !strcmp(state.line1, "error") && state.image_key[0]);
    http_body = "{\"metadata\":{\"error\":\"unrelated\",\"line1\":\"wrong\",\"is_playing\":false,\"image_key\":null},"
                "\"line1\":\"Downstairs\",\"is_playing\":true,\"image_key\":\"cover\"}";
    assert(fetch_now_playing(&state, &power));
    assert(state.is_playing && !strcmp(state.line1, "Downstairs") && state.image_key[0]);
    /* Catch an error-check-only patch that still searches nested media fields. */
    http_body = "{\"metadata\":{\"line1\":\"wrong\",\"is_playing\":false,\"image_key\":null,\"volume\":99,\"length\":999},"
                "\"line1\":\"Breadwinner\",\"is_playing\":true,\"image_key\":\"cover\",\"volume\":12,\"length\":173} \n";
    assert(fetch_now_playing(&state, &power));
    assert(state.is_playing && !strcmp(state.line1, "Breadwinner") && state.image_key[0]);
    assert(state.volume == 0 && s_last_known_volume_fixed && state.length == 173);
    http_body = "{\"error\":null,\"volume\":\"bad\",\"is_playing\":42}";
    assert(!fetch_now_playing(&state, &power));
    assert(state.volume == 0 && state.is_playing && state.length == 173 && state.image_key[0]);
    struct now_playing_state before = state;
    const char *invalid[] = {"{\"error\":\"zone not found\",\"line1\":\"bad\"}",
        "{\"line1\":\"truncated\",\"image_key\":", "[]", "null", "", "{} garbage"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        http_body = invalid[i];
        assert(!fetch_now_playing(&state, &power));
        assert(!memcmp(&state, &before, sizeof(state)));
    }
    http_ok = false;
    assert(!fetch_now_playing(&state, &power));
    assert(!memcmp(&state, &before, sizeof(state)));
    http_ok = true;
    http_body = "{\"line1\":\"Downstairs\",\"is_playing\":true,\"image_key\":\"cover\",\"seek_position\":0}";
    assert(fetch_now_playing(&state, &power));
    assert(!strcmp(state.line1, "Downstairs") && state.is_playing && state.image_key[0]);
}
static void test_zone_refresh_invalidates_old_readiness(void) {
    reset("http://192.168.1.2:8088", true);
    strcpy(fixture.value.zone_id, "roon:room");
    controller_connection_api(&s_connection, true, 1, true, clock_ms);
    parse_zones_from_response("{\"zones\":[]}", 12, NULL);
    assert(!controller_connection_ready(&s_connection));
    assert(s_connection.reachable && s_connection.zone_count == 0);
    parse_zones_from_response("{\"error\":\"unavailable\"}", 23, NULL);
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
static void test_discovery_waits_for_initialization(void) {
 reset("", true); mdns_ready=false;
 for (int i=0;i<5;i++) { update_connection(); clock_ms+=2000; }
 assert(discovery_calls==0 && resolve_calls==0 && http_calls==0);
 assert(s_connection.failures==0 && s_connection.next_attempt_ms==0);
 mdns_ready=true;update_connection();assert(discovery_calls==1);
 reset("http://192.168.1.2:8088",false);mdns_ready=false;
 update_connection();assert(http_calls==1);mdns_ready=true;
}
static void test_refresh_keeps_working_address(void) {
    reset("http://nas2.local:8088", true);
    strcpy(fixture.value.zone_id, "roon:room");
    http_body = "{\"zones\":[{\"zone_id\":\"roon:room\"}]}";
    assert(update_connection());
    clock_ms = s_connection.next_attempt_ms;
    resolution_ok = false;
    unsigned queries = resolve_calls;
    assert(update_connection());
    assert(s_connection.reachable && s_connection.selected_zone_available);
    assert(strcmp(s_connection.endpoint, "http://192.168.1.2:8088") == 0);
    assert(resolve_calls == queries); /* Working HTTP must not require a new DNS answer. */
}
static void test_offline_never_queries(void) {
    reset("http://nas2.local:8088", true);
    atomic_store(&s_network_ready, false);
    assert(!update_connection());
    assert(!discovery_calls && !resolve_calls && !http_calls);
    assert(!s_connection.failures && !s_connection.next_attempt_ms);
}
static void test_wake_reuses_verified_address(void) {
    reset("http://nas2.local:8088", true);
    strcpy(fixture.value.zone_id, "roon:room");
    http_body = "{\"zones\":[{\"zone_id\":\"roon:room\"}]}";
    assert(update_connection());
    assert(retained_cache.version == 1);
    memset(&s_connection, 0, sizeof(s_connection)); s_connection_network[0] = 0;
    resolution_ok = false; mdns_ready = false;
    unsigned queries = discovery_calls + resolve_calls;
    assert(update_connection());
    assert(controller_connection_ready(&s_connection));
    assert(discovery_calls + resolve_calls == queries);
    assert(strcmp(fixture.value.bridge_base, "http://nas2.local:8088") == 0);
    assert(writes == 0); mdns_ready = true;
}
static void test_stale_cache_and_network_change(void) {
    test_wake_reuses_verified_address();
    memset(&s_connection, 0, sizeof(s_connection)); s_connection_network[0] = 0;
    /* A different UHC at the cached address must not replace our zone. */
    http_body = "{\"zones\":[{\"zone_id\":\"roon:other\"}]}";
    assert(!update_connection());
    assert(!controller_connection_ready(&s_connection));
    assert(strcmp(fixture.value.zone_id, "roon:room") == 0);
    memset(&s_connection, 0, sizeof(s_connection));
    strcpy(network_context, "hotel|10.0.0.1|255.255.255.0");
    unsigned requests = http_calls;
    assert(!update_connection());
    assert(http_calls == requests); /* Never probe home cache on another LAN. */
    /* DHCP move: only fresh selected-service discovery chooses a new address. */
    clock_ms = s_connection.next_attempt_ms;
    advertised.seen = true; strcpy(advertised.identity, "http://nas2.local:8088");
    strcpy(advertised.endpoint, "http://10.0.0.55:8088");
    http_body = "{\"zones\":[{\"zone_id\":\"roon:room\"}]}";
    assert(update_connection());
    assert(strcmp(s_connection.endpoint, "http://10.0.0.55:8088") == 0);
}
static void test_disconnect_fences_inflight_probe(void) {
    reset("http://nas2.local:8088", true);
    disconnect_during_http = true;
    assert(!update_connection());
    assert(!s_connection.reachable && !retained_cache.version);
}
static void test_legacy_automatic_ip_keeps_hostname_recovery(void) {
    reset("http://192.168.1.2:8088", true);
    strcpy(fixture.value.zone_id, "roon:room");
    http_body = "{\"zones\":[{\"zone_id\":\"roon:room\"}]}";
    advertised.seen = true;
    strcpy(advertised.identity, "http://nas2.local:8088");
    strcpy(advertised.endpoint, "http://192.168.1.2:8088");
    assert(update_connection());
    assert(queued); /* Reusing an old IP must not prevent learning its durable name. */
    { void (*cb)(void *) = queued; void *arg = queued_arg; queued = NULL; queued_arg = NULL; cb(arg); }
    assert(strcmp(fixture.value.bridge_base, "http://nas2.local:8088") == 0);
    assert(strcmp(retained_cache.selected, fixture.value.bridge_base) == 0);
    memset(&s_connection, 0, sizeof(s_connection)); s_connection_network[0] = 0;
    advertised = (platform_mdns_observation_t){0}; resolution_ok = false;
    assert(update_connection()); /* Learned name and address survive restart together. */
    assert(controller_connection_ready(&s_connection));
}
static void test_complete_snapshot_and_authority(void) {
    reset("http://192.168.1.2:8088", false); strcpy(fixture.value.zone_id, "roon:room");
    struct now_playing_state state = {0}; platform_power_snapshot_t power = {.battery_level = -1};
    const char *valid = "{\"zone_id\":\"roon:room\",\"line1\":\"Song\",\"line2\":\"Artist\",\"is_playing\":true,\"image_key\":\"art\",\"volume_type\":\"db\",\"volume\":-30,\"volume_min\":-80,\"volume_max\":0,\"volume_step\":0.5}";
    http_body = valid; assert(fetch_now_playing(&state, &power));
    assert(!s_last_known_volume_fixed && state.volume == -30 && state.image_key[0]);
    http_body = "{\"line1\":\"Legacy\"}";
    assert(fetch_now_playing(&state, &power));
    assert(!state.is_playing && !state.line2[0] && !state.image_key[0] && !state.length);
    assert(s_last_known_volume_fixed && state.volume_step == 0);
    const char *bad[] = {"{}", "{\"zone_id\":\"roon:other\",\"line1\":\"Wrong room\"}",
        "{\"zone_id\":null,\"line1\":\"Bad\"}", "{\"is_playing\":42}",
        "{\"line1\":\"one\",\"line1\":\"two\"}", "{\"line1\":\"bad\\u0000suffix\"}"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        struct now_playing_state before = state;
        http_body = bad[i]; assert(!fetch_now_playing(&state, &power));
        assert(!memcmp(&before, &state, sizeof(state)) && s_last_known_volume_fixed);
    }
    const char *numbers[] = {"null", "\"bad\"", "1e309", "-1e309", "1e100", "1", "-81"};
    for (size_t i = 0; i < sizeof(numbers)/sizeof(numbers[0]); ++i) {
        char body[256];
        snprintf(body, sizeof(body), "{\"line1\":\"Song\",\"volume_type\":\"db\",\"volume\":%s,\"volume_min\":-80,\"volume_max\":0,\"volume_step\":1}", numbers[i]);
        http_body = body; assert(fetch_now_playing(&state, &power));
        assert(s_last_known_volume_fixed && state.volume_step == 0 && isfinite(state.volume));
    }
    const char *tuples[] = {
        "{\"volume_type\":\"db\",\"volume\":-30,\"volume_min\":0,\"volume_max\":-80,\"volume_step\":1}",
        "{\"volume_type\":\"db\",\"volume\":-30,\"volume_min\":-80,\"volume_max\":0,\"volume_step\":0}",
        "{\"volume_type\":\"db\",\"volume\":-30,\"volume_min\":-80,\"volume_max\":0,\"volume_step\":100}",
        "{\"volume_type\":\"db\",\"volume\":-30,\"volume_min\":-80,\"volume_max\":0}"};
    for (size_t i=0;i<sizeof(tuples)/sizeof(tuples[0]);++i) {
        http_body=tuples[i]; assert(fetch_now_playing(&state,&power)); assert(s_last_known_volume_fixed);
    }
    http_body = "{\"line1\":\"Idle\",\"seek_position\":-1,\"length\":1e309}";
    assert(fetch_now_playing(&state, &power) && state.seek_position == 0 && state.length == 0);
    http_body = "{\"line1\":\"Idle\",\"seek_position\":2.5,\"length\":2147483648}";
    assert(fetch_now_playing(&state, &power) && state.seek_position == 0 && state.length == 0);
    http_body = valid; assert(fetch_now_playing(&state, &power));
    controller_connection_api(&s_connection, true, 1, true, clock_ms);
    s_connection.resolved = true;
    controller_command_t command = controller_command_adjust_volume(1);
    clock_ms += BRIDGE_MEDIA_MAX_AGE_MS + 1;
    assert(bridge_client_execute_command(&command) && controls == 0);
}
static void test_full_artwork_identity(void) {
    reset("http://192.168.1.2:8088", false); strcpy(fixture.value.zone_id, "lms:room");
    struct now_playing_state state = {0}; platform_power_snapshot_t power = {.battery_level = -1};
    char key[400]; memset(key, 'a', sizeof(key)-1); key[sizeof(key)-1]=0;
    memcpy(key, "http://art.example/", 19);
    char body[600]; snprintf(body,sizeof(body),"{\"line1\":\"LMS\",\"image_key\":\"%s\"}",key);
    http_body=body; assert(fetch_now_playing(&state,&power));
    char first[128]; strcpy(first,state.image_key);
    assert(fetch_now_playing(&state,&power) && !strcmp(first,state.image_key));
    key[350]='b'; snprintf(body,sizeof(body),"{\"line1\":\"OpenHome\",\"image_key\":\"%s\"}",key);
    assert(fetch_now_playing(&state,&power) && strcmp(first,state.image_key));
    assert(!strcmp(s_artwork_identity,key));
    s_artwork_identity_generation=0; free(s_artwork_identity); s_artwork_identity=NULL;
    assert(fetch_now_playing(&state,&power) && strcmp(first,state.image_key));
    char *large = malloc(BRIDGE_ARTWORK_KEY_MAX_BYTES + 100);
    strcpy(large,"{\"line1\":\"Oversized\",\"image_key\":\"");
    size_t offset=strlen(large); memset(large+offset,'x',BRIDGE_ARTWORK_KEY_MAX_BYTES+1);
    strcpy(large+offset+BRIDGE_ARTWORK_KEY_MAX_BYTES+1,"\"}");
    http_body=large; assert(!fetch_now_playing(&state,&power)); free(large);
}
static void test_response_fences(void) {
    reset("http://192.168.1.2:8088", false); strcpy(fixture.value.zone_id,"roon:room");
    struct now_playing_state state={0}; platform_power_snapshot_t power={.battery_level=-1};
    http_body="{\"line1\":\"Current\"}"; assert(fetch_now_playing(&state,&power));
    for(int mode=1;mode<=3;++mode) {
        struct now_playing_state before=state; response_race=mode;
        assert(!fetch_now_playing(&state,&power));
        assert(!memcmp(&before,&state,sizeof(state)) && s_response_failure==RESPONSE_STALE);
        response_race=0;
    }
    disconnect_during_http=true; assert(!fetch_now_playing(&state,&power));
    disconnect_during_http=false; atomic_store(&s_network_ready,true);
    assert(fetch_now_playing(&state,&power));
    pending_media_t *pending=calloc(1,sizeof(*pending)); pending->context=state.context;
    unsigned before=media_updates;
    ++fixture.revision; ui_update_cb(pending); assert(media_updates==before);
    assert(fetch_now_playing(&state,&power));
    pending=calloc(1,sizeof(*pending)); pending->context=state.context;
    assert(fetch_now_playing(&state,&power)); ui_update_cb(pending); assert(media_updates==before);
    pending=calloc(1,sizeof(*pending)); pending->context=state.context;
    clock_ms += BRIDGE_MEDIA_MAX_AGE_MS+1; ui_update_cb(pending); assert(media_updates==before);
    assert(fetch_now_playing(&state,&power));
    post_ui_update(&state); assert(media_updates==before+1);
    http_body="{\"zones\":[{\"zone_id\":\"roon:room\"}]}";
    assert(refresh_zone_label(true)); int count=s_state.zone_count;
    pending_zones_t *zone_pending=calloc(1,sizeof(*zone_pending));
    assert(capture_response_context(&zone_pending->context));
    zone_pending->generation=s_zone_name_generation; unsigned name_before=zone_name_updates;
    ++fixture.revision; ui_zone_name_cb(zone_pending); assert(zone_name_updates==name_before);
    zone_pending=calloc(1,sizeof(*zone_pending)); assert(capture_response_context(&zone_pending->context));
    zone_pending->generation=s_zone_count_generation; unsigned count_before=zone_count_updates;
    ++s_zone_count_generation; ui_zone_count_cb(zone_pending); assert(zone_count_updates==count_before);
    response_race=1; http_body="{\"zones\":[]}";
    assert(!refresh_zone_label(true) && s_state.zone_count==count); response_race=0;
}
static void test_zone_inventory_contract(void) {
    reset("http://192.168.1.2:8088", false); strcpy(fixture.value.zone_id,"selected");
    const char *mixed="{\"zones\":[null,{\"zone_id\":\"selected\"},{\"zone_name\":\"missing ID\"}]}";
    http_body=mixed; controller_connection_t probe={0};
    assert(probe_connection(fixture.value.bridge_base,&probe));
    assert(probe.zone_count==1 && probe.selected_zone_available);
    assert(parse_zones_from_response(mixed,strlen(mixed),NULL) && s_state.zone_count==1);
    assert(!strcmp(s_state.zones[0].name,"selected"));
    const char *bad[]={"{\"zones\":[null]}","{\"zones\":[]} trailing", "{\"error\":\"bad\",\"zones\":[]}",
        "{\"zones\":[{\"zone_id\":\"selected\"},{\"zone_id\":\"selected\"}]}",
        "{\"zones\":[{\"zone_id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}]}"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        assert(!parse_zones_from_response(bad[i],strlen(bad[i]),NULL) && s_state.zone_count==1);
        assert(!s_connection.zones_current);
        http_body=bad[i]; assert(!probe_connection(fixture.value.bridge_base,&probe));
    }
    cJSON *root=cJSON_CreateObject(), *list=cJSON_AddArrayToObject(root,"zones");
    for(int i=0;i<MAX_ZONES+1;++i) {
        cJSON *item=cJSON_CreateObject(); char id[32]; snprintf(id,sizeof(id),"room%d",i);
        cJSON_AddStringToObject(item,"zone_id",i==MAX_ZONES?"selected":id); cJSON_AddItemToArray(list,item);
    }
    char *many=cJSON_PrintUnformatted(root); cJSON_Delete(root);
    assert(parse_zones_from_response(many,strlen(many),NULL) && s_state.zone_count==MAX_ZONES);
    assert(!strcmp(s_state.zones[MAX_ZONES-1].id,"selected") && s_connection.selected_zone_available);
    free(many);
    strcpy(s_last_zones_sha,"old"); http_body="{\"zones\":[null]}";
    check_zones_sha("new"); assert(!strcmp(s_last_zones_sha,"old"));
    unsigned calls=http_calls; http_body="{\"zones\":[]}";
    check_zones_sha("new"); assert(http_calls==calls+1 && !strcmp(s_last_zones_sha,"new"));
    assert(s_state.zone_count==0 && !s_connection.selected_zone_available);
}
static void *fail_json_alloc(size_t size) { (void)size; return NULL; }
static void test_response_resource_budget(void) {
    char deep[160]; strcpy(deep,"{\"extension\":"); size_t n=strlen(deep);
    for(int i=0;i<BRIDGE_JSON_MAX_DEPTH;++i) deep[n++]='[';
    deep[n++]='0'; for(int i=0;i<BRIDGE_JSON_MAX_DEPTH;++i) deep[n++]=']'; deep[n++]='}';
    assert(!response_document(deep,n) && s_response_failure==RESPONSE_RESOURCE);
    char embedded[]={ '{','"','x','"',':','0','}',0,'x' };
    assert(!response_document(embedded,sizeof(embedded)));
    char *wide=malloc(20000); strcpy(wide,"{\"x\":["); n=strlen(wide);
    for(int i=0;i<BRIDGE_JSON_MAX_TOKENS+1;++i) { wide[n++]='0'; wide[n++]=','; }
    wide[n-1]=']'; wide[n++]='}'; assert(!response_document(wide,n)); free(wide);
    bridge_alloc_fail_after = 0;
    const char *zones = "{\"zones\":[]}";
    assert(!decode_zones(zones,strlen(zones),""));
    struct now_playing_state state={0}; platform_power_snapshot_t power={.battery_level=-1};
    strcpy(fixture.value.zone_id,"room"); http_body="{\"line1\":\"Track\",\"image_key\":\"cover\"}";
    assert(!fetch_now_playing(&state,&power));
    bridge_alloc_fail_after = -1;
    cJSON_Hooks hooks={.malloc_fn=fail_json_alloc,.free_fn=free}; cJSON_InitHooks(&hooks);
    assert(!response_document("{\"line1\":\"Track\"}",17)); cJSON_InitHooks(NULL);
    char encoded[128]; assert(encode_query_value("roon:a&b?c\"é",encoded,sizeof(encoded)));
    assert(!strcmp(encoded,"roon%3Aa%26b%3Fc%22%C3%A9"));
    assert(!encode_query_value("too long",encoded,3));
    bridge_command_context_t context={.ready=true,.zone_id="room\"\\\n",.volume=INFINITY,.volume_min=0,.volume_max=100,.volume_step=1};
    controller_command_t command=controller_command_adjust_volume(1); bridge_command_plan_t plan;
    assert(bridge_command_plan_build(&command,&context,&plan) && plan.no_op && !plan.updates_volume);
    command.kind=CONTROLLER_COMMAND_TOGGLE_PLAYBACK;
    assert(bridge_command_plan_build(&command,&context,&plan));
    cJSON *root=cJSON_Parse(plan.json); assert(root);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(root,"zone_id")->valuestring,context.zone_id)); cJSON_Delete(root);
}

int main(void) { test_complete_snapshot_and_authority(); test_full_artwork_identity(); test_response_fences(); test_zone_inventory_contract(); test_response_resource_budget();  test_idle_zone_coexists_with_playing_fixed_volume_zone(); test_playback_metadata_cannot_report_a_connection_error(); test_legacy_automatic_ip_keeps_hostname_recovery(); test_wake_reuses_verified_address(); test_stale_cache_and_network_change(); test_disconnect_fences_inflight_probe(); test_refresh_keeps_working_address(); test_offline_never_queries(); test_now_playing_json_strings(); test_fixed_volume_response_and_zone_switch(); test_discovery_waits_for_initialization(); test_zone_cycle_wraps_without_copying_inventory(); test_partial_api_success_preserves_backoff(); test_zone_refresh_invalidates_old_readiness(); test_volume_uses_current_connection_evidence(); test_unresolved_then_recovered(); test_failed_and_stale_candidates(); }
