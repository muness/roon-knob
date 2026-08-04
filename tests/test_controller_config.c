#include "controller_config.h"
#include "platform/platform_storage.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    platform_storage_read_state_t read_state;
    bool needs_persist;
    rk_cfg_t loaded;
    platform_storage_write_result_t write_result;
    rk_cfg_t stored;
    unsigned writes;
} fake_storage_t;

static fake_storage_t s_storage;

/* Immutable byte fixtures model exactly what existing same-device NVS blobs
 * contain.  They deliberately do not construct an rk_cfg_t first: these
 * offsets are the compatibility contract for the shipped V1/V2/V3 layout. */
static const uint8_t s_v1_blob[RK_CFG_V1_SIZE] = {
    [0] = 'v', [1] = '1', [2] = '-', [3] = 's', [4] = 's', [5] = 'i', [6] = 'd',
    [33] = 'v', [34] = '1', [35] = '-', [36] = 'p', [37] = 'a', [38] = 's', [39] = 's',
    [98] = 'h', [99] = 't', [100] = 't', [101] = 'p', [102] = ':', [103] = '/',
    [104] = '/', [105] = 'v', [106] = '1', [107] = '.', [108] = 'e',
    [109] = 'x', [110] = 'a', [111] = 'm', [112] = 'p', [113] = 'l',
    [114] = 'e',
    [226] = 'v', [227] = '1', [228] = '-', [229] = 'z', [230] = 'o',
    [231] = 'n', [232] = 'e',
    [290] = 1,
};

static const uint8_t s_v2_blob[RK_CFG_V2_SIZE] = {
    [0] = 'v', [1] = '2', [2] = '-', [3] = 's', [4] = 's', [5] = 'i', [6] = 'd',
    [33] = 'v', [34] = '2', [35] = '-', [36] = 'p', [37] = 'a', [38] = 's', [39] = 's',
    [98] = 'h', [99] = 't', [100] = 't', [101] = 'p', [102] = ':', [103] = '/',
    [104] = '/', [105] = 'v', [106] = '2', [107] = '.', [108] = 'e',
    [109] = 'x', [110] = 'a', [111] = 'm', [112] = 'p', [113] = 'l',
    [114] = 'e',
    [226] = 'v', [227] = '2', [228] = '-', [229] = 'z', [230] = 'o',
    [231] = 'n', [232] = 'e',
    [290] = 2,
    [291] = 'V', [292] = '2', [293] = ' ', [294] = 'N', [295] = 'a',
    [296] = 'm', [297] = 'e',
    [323] = 'a', [324] = '1', [325] = 'b', [326] = '2',
    [332] = 0x0e, [333] = 0x01, /* rotation_charging = 270 */
    [336] = 1,
    [338] = 0x2c, [339] = 0x01, /* art timeout = 300 */
    [368] = 1,
    [370] = 75,
    [372] = 1,
    [373] = 0xa5, /* V2 trailing padding, now first V3 byte */
};

static const uint8_t s_v3_blob[sizeof(rk_cfg_t)] = {
    [0] = 'v', [1] = '3', [2] = '-', [3] = 'p', [4] = 'r', [5] = 'i',
    [6] = 'm', [7] = 'a', [8] = 'r', [9] = 'y',
    [33] = 'v', [34] = '3', [35] = '-', [36] = 'p', [37] = 'a', [38] = 's', [39] = 's',
    [98] = 'h', [99] = 't', [100] = 't', [101] = 'p', [102] = ':', [103] = '/',
    [104] = '/', [105] = 'v', [106] = '3', [107] = '.', [108] = 'e',
    [109] = 'x', [110] = 'a', [111] = 'm', [112] = 'p', [113] = 'l',
    [114] = 'e',
    [226] = 'v', [227] = '3', [228] = '-', [229] = 'z', [230] = 'o',
    [231] = 'n', [232] = 'e',
    [290] = 3,
    [291] = 'V', [292] = '3', [293] = ' ', [294] = 'N', [295] = 'a',
    [296] = 'm', [297] = 'e',
    [323] = 'c', [324] = '3', [325] = 'd', [326] = '4',
    [332] = 90,
    [334] = 0x0e, [335] = 0x01, /* rotation_not_charging = 270 */
    [368] = 1,
    [370] = 63,
    [372] = 1,
    [373] = 'v', [374] = '3', [375] = '-', [376] = 'p', [377] = 'r',
    [378] = 'i', [379] = 'm', [380] = 'a', [381] = 'r', [382] = 'y',
    [406] = 'v', [407] = '3', [408] = '-', [409] = 'p', [410] = 'a',
    [411] = 's', [412] = 's',
    [471] = 'v', [472] = '3', [473] = '-', [474] = 'g', [475] = 'u',
    [476] = 'e', [477] = 's', [478] = 't',
    [504] = 'g', [505] = 'u', [506] = 'e', [507] = 's', [508] = 't',
    [509] = '-', [510] = 'p', [511] = 'a', [512] = 's', [513] = 's',
    [569] = 2,
};

static void fake_storage_reset(void) {
    s_storage = (fake_storage_t){
        .read_state = PLATFORM_STORAGE_READ_EMPTY,
        .write_result = PLATFORM_STORAGE_COMMITTED_VERIFIED,
    };
}

bool platform_storage_read(rk_cfg_t *out,
                           platform_storage_read_result_t *out_result) {
    if (out) {
        *out = s_storage.loaded;
    }
    if (out_result) {
        *out_result = (platform_storage_read_result_t){
            .state = s_storage.read_state,
            .needs_persist = s_storage.needs_persist,
        };
    }
    return s_storage.read_state != PLATFORM_STORAGE_READ_ERROR;
}

platform_storage_write_result_t platform_storage_write(const rk_cfg_t *in) {
    assert(in);
    s_storage.writes++;
    if (s_storage.write_result != PLATFORM_STORAGE_NOT_COMMITTED) {
        assert(rk_cfg_canonicalize_v3(&s_storage.stored, in));
    }
    return s_storage.write_result;
}

void platform_storage_defaults(rk_cfg_t *out) {
    assert(out);
    *out = (rk_cfg_t){0};
    rk_cfg_set_display_defaults(out);
    out->cfg_ver = RK_CFG_CURRENT_VER;
}

static rk_cfg_t make_full_config(void) {
    rk_cfg_t cfg;
    platform_storage_defaults(&cfg);
    rk_strlcpy(cfg.bridge_base, "http://bridge.example", sizeof(cfg.bridge_base));
    rk_strlcpy(cfg.zone_id, "zone-main", sizeof(cfg.zone_id));
    rk_strlcpy(cfg.knob_name, "Living Room", sizeof(cfg.knob_name));
    rk_strlcpy(cfg.config_sha, "deadbeef", sizeof(cfg.config_sha));
    assert(rk_cfg_add_wifi(&cfg, "home", "secret") == 0);
    assert(rk_cfg_add_wifi(&cfg, "guest", "guest-secret") == 1);
    rk_cfg_sync_primary_wifi(&cfg);
    cfg.rotation_charging = 90;
    cfg.rotation_not_charging = 270;
    cfg.art_mode_charging_enabled = 0;
    cfg.art_mode_charging_timeout_sec = 61;
    cfg.art_mode_battery_enabled = 1;
    cfg.art_mode_battery_timeout_sec = 31;
    cfg.dim_charging_enabled = 0;
    cfg.dim_charging_timeout_sec = 121;
    cfg.dim_battery_enabled = 1;
    cfg.dim_battery_timeout_sec = 32;
    cfg.sleep_charging_enabled = 1;
    cfg.sleep_charging_timeout_sec = 1;
    cfg.sleep_battery_enabled = 0;
    cfg.sleep_battery_timeout_sec = 62;
    cfg.deep_sleep_charging_enabled = 1;
    cfg.deep_sleep_charging_timeout_sec = 1201;
    cfg.deep_sleep_battery_enabled = 0;
    cfg.deep_sleep_battery_timeout_sec = 1202;
    cfg.wifi_power_save_enabled = 1;
    cfg.cpu_freq_scaling_enabled = 1;
    cfg.sleep_poll_stopped_sec = 63;
    cfg.bridge_from_mdns = 1;
    return cfg;
}

static void reset_owner_with(const rk_cfg_t *cfg,
                             platform_storage_read_state_t read_state,
                             bool needs_persist,
                             platform_storage_write_result_t write_result) {
    controller_config_reset_for_test();
    fake_storage_reset();
    s_storage.read_state = read_state;
    s_storage.needs_persist = needs_persist;
    s_storage.write_result = write_result;
    if (cfg) {
        s_storage.loaded = *cfg;
    }
}

static controller_config_snapshot_t current_snapshot(void) {
    controller_config_snapshot_t snapshot;
    assert(controller_config_snapshot(&snapshot));
    return snapshot;
}

static void test_init_empty_defaults(void) {
    reset_owner_with(NULL, PLATFORM_STORAGE_READ_EMPTY, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());
    controller_config_snapshot_t snapshot = current_snapshot();
    assert(snapshot.durability == CONTROLLER_CONFIG_DURABILITY_DURABLE);
    assert(snapshot.revision == 1);
    assert(snapshot.value.cfg_ver == RK_CFG_CURRENT_VER);
    assert(s_storage.writes == 1);
#if defined(TEST_COMPILED_DEFAULTS)
    assert(snapshot.value.wifi_count == 1);
    assert(strcmp(snapshot.value.ssid, "compiled-ssid") == 0);
    assert(strcmp(snapshot.value.pass, "compiled-pass") == 0);
#else
    assert(snapshot.value.wifi_count == 0);
#endif
}

static void test_existing_and_recovered_configs(void) {
    rk_cfg_t original = make_full_config();
    reset_owner_with(&original, PLATFORM_STORAGE_READ_VALID, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());
    controller_config_snapshot_t snapshot = current_snapshot();
    assert(snapshot.durability == CONTROLLER_CONFIG_DURABILITY_DURABLE);
    assert(rk_cfg_v3_equal(&snapshot.value, &original));
    assert(s_storage.writes == 0);

    reset_owner_with(&original, PLATFORM_STORAGE_READ_RECOVERED, true,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());
    snapshot = current_snapshot();
    assert(snapshot.durability == CONTROLLER_CONFIG_DURABILITY_DURABLE);
    assert(rk_cfg_v3_equal(&snapshot.value, &original));
    assert(s_storage.writes == 1);

    rk_cfg_t without_wifi = make_full_config();
    rk_cfg_remove_wifi(&without_wifi, 1);
    rk_cfg_remove_wifi(&without_wifi, 0);
    rk_cfg_sync_primary_wifi(&without_wifi);
    reset_owner_with(&without_wifi, PLATFORM_STORAGE_READ_VALID, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());
    snapshot = current_snapshot();
#if defined(TEST_COMPILED_DEFAULTS)
    assert(snapshot.value.wifi_count == 1);
    assert(strcmp(snapshot.value.ssid, "compiled-ssid") == 0);
    assert(s_storage.writes == 1);
#else
    assert(snapshot.value.wifi_count == 0);
    assert(s_storage.writes == 0);
#endif
}

static void test_same_device_v1_v2_v3_and_default_preparation(void) {
    rk_cfg_t v1 = {0};
    memcpy(&v1, s_v1_blob, sizeof(s_v1_blob));
    assert(rk_cfg_prepare_storage_read(&v1, RK_CFG_V1_SIZE));
    assert(v1.cfg_ver == RK_CFG_CURRENT_VER);
    assert(v1.wifi_count == 1);
    assert(strcmp(v1.wifi[0].ssid, "v1-ssid") == 0);
    assert(strcmp(v1.bridge_base, "http://v1.example") == 0);
    assert(v1.rotation_charging == RK_DEFAULT_ROTATION_CHARGING);

    rk_cfg_t v2 = {0};
    memcpy(&v2, s_v2_blob, sizeof(s_v2_blob));
    assert(rk_cfg_prepare_storage_read(&v2, RK_CFG_V2_SIZE));
    assert(v2.cfg_ver == RK_CFG_CURRENT_VER);
    assert(v2.wifi_count == 1);
    assert(strcmp(v2.wifi[0].ssid, "v2-ssid") == 0);
    assert(strcmp(v2.knob_name, "V2 Name") == 0);
    assert(v2.rotation_charging == 270);
    assert(v2.art_mode_charging_timeout_sec == 300);
    assert(v2.wifi_power_save_enabled == 1);
    assert(v2.sleep_poll_stopped_sec == 75);
    assert(v2.bridge_from_mdns == 1);

    rk_cfg_t v3 = {0};
    memcpy(&v3, s_v3_blob, sizeof(s_v3_blob));
    assert(!rk_cfg_prepare_storage_read(&v3, sizeof(v3)));
    assert(v3.cfg_ver == RK_CFG_CURRENT_VER);
    assert(strcmp(v3.ssid, "v3-primary") == 0);
    assert(strcmp(v3.wifi[0].ssid, "v3-primary") == 0);
    assert(strcmp(v3.wifi[1].ssid, "v3-guest") == 0);
    assert(strcmp(v3.wifi[1].pass, "guest-pass") == 0);
    assert(v3.rotation_charging == 90);
    assert(v3.rotation_not_charging == 270);
    assert(v3.wifi_count == 2);

    rk_cfg_t full_size_old_marker = make_full_config();
    full_size_old_marker.cfg_ver = 2;
    assert(rk_cfg_prepare_storage_read(&full_size_old_marker,
                                       sizeof(full_size_old_marker)));
    assert(full_size_old_marker.cfg_ver == RK_CFG_CURRENT_VER);
    assert(strcmp(full_size_old_marker.wifi[1].ssid, "guest") == 0);

    rk_cfg_t malformed = make_full_config();
    assert(rk_cfg_prepare_storage_read(&malformed, sizeof(malformed) - 1));
    assert(malformed.cfg_ver == RK_CFG_CURRENT_VER);
    assert(malformed.wifi_count == 0);
    assert(malformed.bridge_base[0] == '\0');
}

static void test_init_durability_outcomes(void) {
    reset_owner_with(NULL, PLATFORM_STORAGE_READ_EMPTY, false,
                     PLATFORM_STORAGE_COMMITTED_UNVERIFIED);
    assert(controller_config_init());
    controller_config_snapshot_t snapshot = current_snapshot();
    assert(snapshot.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT);
    assert(snapshot.value.cfg_ver == RK_CFG_CURRENT_VER);
    assert(s_storage.writes == 1);

    rk_cfg_t recovered = make_full_config();
    rk_strlcpy(recovered.zone_id, "preserve-on-repair-failure",
               sizeof(recovered.zone_id));
    reset_owner_with(&recovered, PLATFORM_STORAGE_READ_RECOVERED, true,
                     PLATFORM_STORAGE_NOT_COMMITTED);
    assert(controller_config_init());
    snapshot = current_snapshot();
    assert(snapshot.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY);
    assert(rk_cfg_v3_equal(&snapshot.value, &recovered));
    assert(strcmp(snapshot.value.zone_id, "preserve-on-repair-failure") == 0);
    assert(s_storage.writes == 1);

    reset_owner_with(NULL, PLATFORM_STORAGE_READ_ERROR, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());
    snapshot = current_snapshot();
    assert(snapshot.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY);
    assert(snapshot.value.cfg_ver == RK_CFG_CURRENT_VER);
    assert(s_storage.writes == 0);

    reset_owner_with(NULL, PLATFORM_STORAGE_READ_EMPTY, false,
                     PLATFORM_STORAGE_NOT_COMMITTED);
    assert(controller_config_init());
    snapshot = current_snapshot();
    assert(snapshot.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY);
    assert(snapshot.value.cfg_ver == RK_CFG_CURRENT_VER);
    assert(s_storage.writes == 1);
}

static controller_remote_preferences_t all_remote_preferences(void) {
    controller_remote_preferences_t preferences = {
        .present = CONTROLLER_REMOTE_PREFERENCE_ALL,
        .rotation_charging = 180,
        .rotation_not_charging = 0,
        .art_mode_charging_enabled = 1,
        .art_mode_charging_timeout_sec = 600,
        .art_mode_battery_enabled = 0,
        .art_mode_battery_timeout_sec = 601,
        .dim_charging_enabled = 1,
        .dim_charging_timeout_sec = 602,
        .dim_battery_enabled = 0,
        .dim_battery_timeout_sec = 603,
        .sleep_charging_enabled = 1,
        .sleep_charging_timeout_sec = 604,
        .sleep_battery_enabled = 0,
        .sleep_battery_timeout_sec = 605,
        .deep_sleep_charging_enabled = 1,
        .deep_sleep_charging_timeout_sec = 606,
        .deep_sleep_battery_enabled = 0,
        .deep_sleep_battery_timeout_sec = 607,
        .wifi_power_save_enabled = 0,
        .cpu_freq_scaling_enabled = 1,
        .sleep_poll_stopped_sec = 608,
    };
    rk_strlcpy(preferences.knob_name, "Remote Name",
               sizeof(preferences.knob_name));
    rk_strlcpy(preferences.config_sha, "cafebabe",
               sizeof(preferences.config_sha));
    return preferences;
}

static void test_typed_mutations_preserve_unowned_fields(void) {
    rk_cfg_t original = make_full_config();
    rk_cfg_remove_wifi(&original, 1);
    rk_cfg_sync_primary_wifi(&original);
    reset_owner_with(&original, PLATFORM_STORAGE_READ_VALID, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());

    controller_config_snapshot_t committed;
    assert(controller_config_upsert_wifi("office", "office-pass", false,
                                         &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(committed.value.wifi_count == 2);
    assert(strcmp(committed.value.wifi[1].ssid, "office") == 0);
    assert(strcmp(committed.value.bridge_base, original.bridge_base) == 0);
    assert(strcmp(committed.value.zone_id, original.zone_id) == 0);
    assert(strcmp(committed.value.knob_name, original.knob_name) == 0);

    assert(controller_config_upsert_wifi("office", "updated-office", true,
                                         &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.wifi[0].ssid, "office") == 0);
    assert(strcmp(committed.value.pass, "updated-office") == 0);
    assert(controller_config_promote_wifi(1, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.wifi[0].ssid, "home") == 0);
    assert(controller_config_remove_wifi(1, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(committed.value.wifi_count == 1);
    assert(strcmp(committed.value.bridge_base, original.bridge_base) == 0);
    assert(strcmp(committed.value.zone_id, original.zone_id) == 0);
    assert(strcmp(committed.value.knob_name, original.knob_name) == 0);

    assert(controller_config_set_endpoint("http://manual.example/", false,
                                          &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.bridge_base, "http://manual.example") == 0);
    assert(committed.value.bridge_from_mdns == 0);
    assert(strcmp(committed.value.ssid, "home") == 0);
    assert(strcmp(committed.value.zone_id, original.zone_id) == 0);

    assert(controller_config_set_zone("zone-new", &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.zone_id, "zone-new") == 0);
    assert(strcmp(committed.value.bridge_base, "http://manual.example") == 0);

    controller_remote_preferences_t preferences = all_remote_preferences();
    assert(controller_config_merge_remote_preferences(&preferences, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.knob_name, "Remote Name") == 0);
    assert(strcmp(committed.value.config_sha, "cafebabe") == 0);
    assert(strcmp(committed.value.ssid, "home") == 0);
    assert(strcmp(committed.value.zone_id, "zone-new") == 0);
    assert(committed.revision == 8);

    controller_remote_preferences_t partial = {
        .present = CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA,
    };
    rk_strlcpy(partial.config_sha, "facefeed", sizeof(partial.config_sha));
    assert(controller_config_merge_remote_preferences(&partial, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.config_sha, "facefeed") == 0);
    assert(strcmp(committed.value.knob_name, "Remote Name") == 0);
    assert(committed.value.rotation_charging == 180);
    assert(committed.revision == 9);

    controller_config_wifi_snapshot_t wifi;
    assert(controller_config_wifi_snapshot(&wifi));
    assert(wifi.count == 1);
    assert(strcmp(wifi.entries[0].ssid, "home") == 0);
    assert(wifi.revision == committed.revision);
}

static void test_write_outcomes_and_all_or_nothing_validation(void) {
    rk_cfg_t original = make_full_config();
    reset_owner_with(&original, PLATFORM_STORAGE_READ_VALID, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());
    controller_config_snapshot_t before = current_snapshot();
    controller_config_snapshot_t committed;

    s_storage.write_result = PLATFORM_STORAGE_NOT_COMMITTED;
    assert(controller_config_set_zone("not-published", &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    controller_config_snapshot_t after_failed_write = current_snapshot();
    assert(rk_cfg_v3_equal(&after_failed_write.value, &before.value));

    s_storage.write_result = PLATFORM_STORAGE_COMMITTED_UNVERIFIED;
    assert(controller_config_set_zone("degraded-zone", &committed) ==
           CONTROLLER_CONFIG_COMMITTED_UNVERIFIED);
    assert(committed.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT);
    assert(strcmp(committed.value.zone_id, "degraded-zone") == 0);

    s_storage.write_result = PLATFORM_STORAGE_COMMITTED_VERIFIED;
    assert(controller_config_set_zone("durable-zone", &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(committed.durability == CONTROLLER_CONFIG_DURABILITY_DURABLE);

    controller_config_snapshot_t stable = current_snapshot();
    unsigned writes_before = s_storage.writes;
    char unterminated_ssid[sizeof(((rk_wifi_entry_t *)0)->ssid)];
    memset(unterminated_ssid, 'x', sizeof(unterminated_ssid));
    assert(controller_config_upsert_wifi(unterminated_ssid, "pass", false,
                                         &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    assert(controller_config_set_endpoint(NULL, false, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    assert(controller_config_set_zone(NULL, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);

    controller_remote_preferences_t invalid = all_remote_preferences();
    invalid.rotation_charging = 45;
    assert(controller_config_merge_remote_preferences(&invalid, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    invalid = all_remote_preferences();
    invalid.dim_charging_enabled = 2;
    assert(controller_config_merge_remote_preferences(&invalid, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    invalid = all_remote_preferences();
    invalid.dim_battery_enabled = 2;
    assert(controller_config_merge_remote_preferences(&invalid, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    invalid = all_remote_preferences();
    invalid.sleep_poll_stopped_sec = UINT16_MAX + 1u;
    assert(controller_config_merge_remote_preferences(&invalid, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    invalid = all_remote_preferences();
    memset(invalid.config_sha, 'f', sizeof(invalid.config_sha));
    assert(controller_config_merge_remote_preferences(&invalid, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);

    /* A mixed payload is atomic: a good field cannot leak through beside a
     * bad one.  The bridge parser has the same all-or-nothing contract. */
    invalid = (controller_remote_preferences_t){
        .present = CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME |
                   CONTROLLER_REMOTE_PREFERENCE_ROTATION_CHARGING,
        .rotation_charging = 45,
    };
    rk_strlcpy(invalid.knob_name, "must-not-publish",
               sizeof(invalid.knob_name));
    assert(controller_config_merge_remote_preferences(&invalid, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    assert(s_storage.writes == writes_before);
    controller_config_snapshot_t after_invalid_request = current_snapshot();
    assert(rk_cfg_v3_equal(&after_invalid_request.value, &stable.value));
}

static void test_endpoint_and_remote_write_outcomes(void) {
    rk_cfg_t original = make_full_config();
    reset_owner_with(&original, PLATFORM_STORAGE_READ_VALID, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());

    controller_config_snapshot_t before = current_snapshot();
    controller_config_snapshot_t committed;
    controller_config_endpoint_token_t endpoint;
    assert(controller_config_capture_endpoint_token(&endpoint));

    s_storage.write_result = PLATFORM_STORAGE_NOT_COMMITTED;
    assert(controller_config_set_endpoint_if_current(
               &endpoint, "http://not-committed.example", true, false,
               &committed) == CONTROLLER_CONFIG_NOT_COMMITTED);
    controller_config_snapshot_t after_not_committed = current_snapshot();
    assert(rk_cfg_v3_equal(&after_not_committed.value, &before.value));

    assert(controller_config_capture_endpoint_token(&endpoint));
    s_storage.write_result = PLATFORM_STORAGE_COMMITTED_UNVERIFIED;
    assert(controller_config_set_endpoint_if_current(
               &endpoint, "http://unverified.example", true, false,
               &committed) == CONTROLLER_CONFIG_COMMITTED_UNVERIFIED);
    assert(committed.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT);
    assert(strcmp(committed.value.bridge_base, "http://unverified.example") == 0);
    assert(committed.value.bridge_from_mdns == 1);

    s_storage.write_result = PLATFORM_STORAGE_COMMITTED_VERIFIED;
    assert(controller_config_set_endpoint("http://verified.example", false,
                                          &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(committed.durability == CONTROLLER_CONFIG_DURABILITY_DURABLE);
    assert(strcmp(committed.value.bridge_base, "http://verified.example") == 0);

    controller_remote_preferences_t preferences = {
        .present = CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME |
                   CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA,
    };
    rk_strlcpy(preferences.knob_name, "Remote candidate",
               sizeof(preferences.knob_name));
    rk_strlcpy(preferences.config_sha, "b16b00b5",
               sizeof(preferences.config_sha));

    controller_config_snapshot_t stable = current_snapshot();
    s_storage.write_result = PLATFORM_STORAGE_NOT_COMMITTED;
    assert(controller_config_merge_remote_preferences(&preferences, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    after_not_committed = current_snapshot();
    assert(rk_cfg_v3_equal(&after_not_committed.value, &stable.value));

    s_storage.write_result = PLATFORM_STORAGE_COMMITTED_UNVERIFIED;
    assert(controller_config_merge_remote_preferences(&preferences, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_UNVERIFIED);
    assert(committed.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT);
    assert(strcmp(committed.value.knob_name, "Remote candidate") == 0);
    assert(strcmp(committed.value.config_sha, "b16b00b5") == 0);

    s_storage.write_result = PLATFORM_STORAGE_COMMITTED_VERIFIED;
    assert(controller_config_merge_remote_preferences(&preferences, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(committed.durability == CONTROLLER_CONFIG_DURABILITY_DURABLE);
}

static void assert_every_named_field_is_compared(void) {
    rk_cfg_t base = make_full_config();
    rk_cfg_t changed;
#define ASSERT_CHANGED(expression)                                             \
    do {                                                                       \
        changed = base;                                                        \
        expression;                                                            \
        bool needs_repair = rk_cfg_normalize_v3(&changed);                    \
        assert(needs_repair || !rk_cfg_v3_equal(&base, &changed));            \
    } while (0)
    ASSERT_CHANGED(changed.ssid[0] = 'S');
    ASSERT_CHANGED(changed.pass[0] = 'P');
    ASSERT_CHANGED(changed.bridge_base[0] = 'B');
    ASSERT_CHANGED(changed.zone_id[0] = 'Z');
    ASSERT_CHANGED(changed.cfg_ver = 2);
    ASSERT_CHANGED(changed.knob_name[0] = 'K');
    ASSERT_CHANGED(changed.config_sha[0] = 'C');
    ASSERT_CHANGED(changed.rotation_charging = 0);
    ASSERT_CHANGED(changed.rotation_not_charging = 90);
    ASSERT_CHANGED(changed.art_mode_charging_enabled = 1);
    ASSERT_CHANGED(changed.art_mode_charging_timeout_sec++);
    ASSERT_CHANGED(changed.art_mode_battery_enabled = 0);
    ASSERT_CHANGED(changed.art_mode_battery_timeout_sec++);
    ASSERT_CHANGED(changed.dim_charging_enabled = 1);
    ASSERT_CHANGED(changed.dim_charging_timeout_sec++);
    ASSERT_CHANGED(changed.dim_battery_enabled = 0);
    ASSERT_CHANGED(changed.dim_battery_timeout_sec++);
    ASSERT_CHANGED(changed.sleep_charging_enabled = 0);
    ASSERT_CHANGED(changed.sleep_charging_timeout_sec++);
    ASSERT_CHANGED(changed.sleep_battery_enabled = 1);
    ASSERT_CHANGED(changed.sleep_battery_timeout_sec++);
    ASSERT_CHANGED(changed.deep_sleep_charging_enabled = 0);
    ASSERT_CHANGED(changed.deep_sleep_charging_timeout_sec++);
    ASSERT_CHANGED(changed.deep_sleep_battery_enabled = 1);
    ASSERT_CHANGED(changed.deep_sleep_battery_timeout_sec++);
    ASSERT_CHANGED(changed.wifi_power_save_enabled = 0);
    ASSERT_CHANGED(changed.cpu_freq_scaling_enabled = 0);
    ASSERT_CHANGED(changed.sleep_poll_stopped_sec++);
    ASSERT_CHANGED(changed.bridge_from_mdns = 0);
    ASSERT_CHANGED(changed.wifi[0].ssid[0] = 'H');
    ASSERT_CHANGED(changed.wifi[0].pass[0] = 'Q');
    ASSERT_CHANGED(changed.wifi[1].ssid[0] = 'G');
    ASSERT_CHANGED(changed.wifi[1].pass[0] = 'R');
    ASSERT_CHANGED(changed.wifi_count = 1);
#undef ASSERT_CHANGED
}

static void assert_storage_verification_ignores_padding(void) {
    rk_cfg_t canonical;
    rk_cfg_t noisy = make_full_config();
    assert(rk_cfg_canonicalize_v3(&canonical, &noisy));

    /* These are the alignment bytes before the V2 uint16_t timeout fields.
     * Historical blobs may contain arbitrary values here; they are not named
     * configuration state and must not turn a successful readback into an
     * unverified commit. */
    const size_t padding_offsets[] = {337, 341, 345, 349,
                                      353, 357, 361, 365};
    for (size_t i = 0; i < sizeof(padding_offsets) / sizeof(padding_offsets[0]);
         ++i) {
        ((uint8_t *)&noisy)[padding_offsets[i]] = (uint8_t)(0xa0u + i);
    }

    assert(memcmp(&noisy, &canonical, sizeof(noisy)) != 0);
    assert(rk_cfg_v3_equal_to_canonical(&noisy, &canonical));
}

static void *add_wifi_thread(void *arg) {
    (void)arg;
    assert(controller_config_upsert_wifi("office", "office-pass", false,
                                         NULL) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    return NULL;
}

static void *remote_preferences_thread(void *arg) {
    (void)arg;
    controller_remote_preferences_t preferences = {
        .present = CONTROLLER_REMOTE_PREFERENCE_KNOB_NAME |
                   CONTROLLER_REMOTE_PREFERENCE_ROTATION_CHARGING,
        .rotation_charging = 270,
    };
    rk_strlcpy(preferences.knob_name, "Concurrent", sizeof(preferences.knob_name));
    assert(controller_config_merge_remote_preferences(&preferences, NULL) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    return NULL;
}

static void test_interleaved_mutations_keep_both_field_sets(void) {
    rk_cfg_t original = make_full_config();
    rk_cfg_remove_wifi(&original, 1);
    rk_cfg_sync_primary_wifi(&original);
    reset_owner_with(&original, PLATFORM_STORAGE_READ_VALID, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());
    pthread_t wifi_thread;
    pthread_t remote_thread;
    assert(pthread_create(&wifi_thread, NULL, add_wifi_thread, NULL) == 0);
    assert(pthread_create(&remote_thread, NULL, remote_preferences_thread, NULL) ==
           0);
    assert(pthread_join(wifi_thread, NULL) == 0);
    assert(pthread_join(remote_thread, NULL) == 0);

    controller_config_snapshot_t snapshot = current_snapshot();
    assert(snapshot.value.wifi_count == 2);
    assert(strcmp(snapshot.value.wifi[1].ssid, "office") == 0 ||
           strcmp(snapshot.value.wifi[0].ssid, "office") == 0);
    assert(strcmp(snapshot.value.knob_name, "Concurrent") == 0);
    assert(snapshot.value.rotation_charging == 270);
}

static void test_endpoint_tokens_fail_closed_on_newer_endpoint_decisions(void) {
    rk_cfg_t original = make_full_config();
    reset_owner_with(&original, PLATFORM_STORAGE_READ_VALID, false,
                     PLATFORM_STORAGE_COMMITTED_VERIFIED);
    assert(controller_config_init());

    controller_config_endpoint_token_t stale;
    assert(controller_config_capture_endpoint_token(&stale));
    controller_config_snapshot_t committed;
    assert(controller_config_set_endpoint("http://manual.example", false,
                                          &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(controller_config_set_endpoint_if_current(
               &stale, "http://stale-mdns.example", true, false, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);

    controller_remote_preferences_t preferences = {
        .present = CONTROLLER_REMOTE_PREFERENCE_CONFIG_SHA,
    };
    rk_strlcpy(preferences.config_sha, "stale123", sizeof(preferences.config_sha));
    assert(controller_config_merge_remote_preferences_if_endpoint_current(
               &stale, &preferences, &committed) ==
           CONTROLLER_CONFIG_NOT_COMMITTED);
    controller_config_snapshot_t after_manual = current_snapshot();
    assert(strcmp(after_manual.value.bridge_base, "http://manual.example") == 0);
    assert(strcmp(after_manual.value.config_sha, "deadbeef") == 0);

    controller_config_endpoint_token_t current;
    assert(controller_config_capture_endpoint_token(&current));
    assert(controller_config_set_zone("unrelated-zone", &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(controller_config_set_endpoint_if_current(
               &current, "http://second.example", false, false, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.bridge_base, "http://second.example") == 0);

    assert(controller_config_set_endpoint("", false, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    controller_config_endpoint_token_t clear_token;
    assert(controller_config_capture_endpoint_token(&clear_token));
    assert(controller_config_set_endpoint_if_current(
               &clear_token, "http://mdns.example", true, true, &committed) ==
           CONTROLLER_CONFIG_COMMITTED_VERIFIED);
    assert(strcmp(committed.value.bridge_base, "http://mdns.example") == 0);
    assert(committed.value.bridge_from_mdns == 1);
}

int main(void) {
    test_init_empty_defaults();
    test_existing_and_recovered_configs();
    test_same_device_v1_v2_v3_and_default_preparation();
    test_init_durability_outcomes();
    test_typed_mutations_preserve_unowned_fields();
    test_write_outcomes_and_all_or_nothing_validation();
    test_endpoint_and_remote_write_outcomes();
    assert_every_named_field_is_compared();
    assert_storage_verification_ignores_padding();
    test_interleaved_mutations_keep_both_field_sets();
    test_endpoint_tokens_fail_closed_on_newer_endpoint_decisions();
    puts("controller config owner contracts passed");
    return 0;
}
