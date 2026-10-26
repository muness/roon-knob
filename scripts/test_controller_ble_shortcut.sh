#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=$(mktemp "${TMPDIR:-/tmp}/ble-shortcut.XXXXXX")
headers=$(mktemp -d "${TMPDIR:-/tmp}/ble-shortcut-headers.XXXXXX")
trap 'rm -f "$binary"; rm -rf "$headers"' EXIT
cp tests/fixtures/ble_shortcut/esp_log.inc "$headers/esp_log.h"
cp tests/fixtures/ble_shortcut/esp_timer.inc "$headers/esp_timer.h"
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -I"$headers" -Icommon -Icomponents/rk_ble_hid_host/include -Iinclude \
  tests/test_controller_ble_shortcut.c common/controller_shortcut_key.c -o "$binary"
"$binary"
