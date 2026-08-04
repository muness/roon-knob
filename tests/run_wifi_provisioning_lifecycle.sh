#!/bin/sh
set -eu

fixture_dir="tests/fixtures/provisioning_lifecycle"
fake_include_dir="$(mktemp -d)"
trap 'rm -rf "$fake_include_dir"' EXIT

for header in esp_err.h esp_event.h esp_log.h esp_mac.h esp_netif.h esp_system.h esp_timer.h esp_wifi.h nvs_flash.h; do
    printf '#include "fake_esp_api.inc"\n' > "$fake_include_dir/$header"
done
printf '#define CONFIG_RK_DEFAULT_SSID ""\n#define CONFIG_RK_DEFAULT_PASS ""\n' > "$fake_include_dir/sdkconfig.h"

cc -std=c11 -Wall -Wextra -Werror -pthread \
    -I"$fake_include_dir" -I"$fixture_dir" -Iinclude -Icommon \
    tests/test_wifi_provisioning_lifecycle.c \
    "$fixture_dir/fake_idf.c" common/wifi_manager.c \
    -o /tmp/test-wifi-provisioning-lifecycle
/tmp/test-wifi-provisioning-lifecycle
