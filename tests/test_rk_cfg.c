#include "rk_cfg.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_full_list_requires_explicit_removal(void) {
    rk_cfg_t cfg = {0};

    assert(rk_cfg_add_wifi(&cfg, "first", "one") == 0);
    assert(rk_cfg_add_wifi(&cfg, "second", "two") == 1);
    assert(rk_cfg_add_wifi(&cfg, "third", "three") == -1);

    assert(cfg.wifi_count == 2);
    assert(strcmp(cfg.wifi[0].ssid, "first") == 0);
    assert(strcmp(cfg.wifi[1].ssid, "second") == 0);
}

static void test_update_and_promote(void) {
    rk_cfg_t cfg = {0};

    assert(rk_cfg_add_wifi(&cfg, "first", "one") == 0);
    assert(rk_cfg_add_wifi(&cfg, "second", "two") == 1);
    assert(rk_cfg_add_wifi(&cfg, "second", "updated") == 1);
    assert(rk_cfg_promote_wifi(&cfg, 1));

    assert(cfg.wifi_count == 2);
    assert(strcmp(cfg.wifi[0].ssid, "second") == 0);
    assert(strcmp(cfg.wifi[0].pass, "updated") == 0);
    assert(strcmp(cfg.wifi[1].ssid, "first") == 0);
    assert(strcmp(cfg.ssid, "second") == 0);
    assert(strcmp(cfg.pass, "updated") == 0);
}

static void test_corrupt_count_recovers_legacy_primary(void) {
    rk_cfg_t cfg = {0};
    rk_strlcpy(cfg.ssid, "known-good", sizeof(cfg.ssid));
    rk_strlcpy(cfg.pass, "secret", sizeof(cfg.pass));
    cfg.wifi_count = UINT8_MAX;
    memset(cfg.wifi, 0xa5, sizeof(cfg.wifi));

    assert(rk_cfg_normalize_wifi(&cfg));
    assert(cfg.wifi_count == 1);
    assert(strcmp(cfg.wifi[0].ssid, "known-good") == 0);
    assert(strcmp(cfg.wifi[0].pass, "secret") == 0);
    assert(strcmp(cfg.ssid, "known-good") == 0);
}

static void test_empty_and_invalid_entries_are_safe(void) {
    rk_cfg_t cfg = {0};
    assert(rk_cfg_add_wifi(&cfg, "first", "one") == 0);
    assert(rk_cfg_add_wifi(&cfg, "second", "two") == 1);

    cfg.wifi[0].ssid[0] = '\0';
    assert(rk_cfg_normalize_wifi(&cfg));
    assert(cfg.wifi_count == 1);
    assert(strcmp(cfg.wifi[0].ssid, "second") == 0);
    assert(strcmp(cfg.ssid, "second") == 0);

    cfg.wifi_count = UINT8_MAX;
    rk_cfg_remove_wifi(&cfg, 0);
    assert(cfg.wifi_count == UINT8_MAX);
}

static void test_local_connectivity_merge_preserves_controller_fields(void) {
    rk_cfg_t controller = {0};
    rk_cfg_t local = {0};
    rk_strlcpy(controller.bridge_base, "http://bridge", sizeof(controller.bridge_base));
    rk_strlcpy(controller.zone_id, "zone-1", sizeof(controller.zone_id));
    controller.rotation_charging = 180;

    assert(rk_cfg_add_wifi(&local, "home", "secret") == 0);
    rk_cfg_sync_primary_wifi(&local);
    assert(rk_cfg_copy_local_connectivity(&controller, &local));

    assert(strcmp(controller.ssid, "home") == 0);
    assert(strcmp(controller.bridge_base, "http://bridge") == 0);
    assert(strcmp(controller.zone_id, "zone-1") == 0);
    assert(controller.rotation_charging == 180);
}

int main(void) {
    test_full_list_requires_explicit_removal();
    test_update_and_promote();
    test_corrupt_count_recovers_legacy_primary();
    test_empty_and_invalid_entries_are_safe();
    test_local_connectivity_merge_preserves_controller_fields();
    puts("rk_cfg contract tests passed");
    return 0;
}
