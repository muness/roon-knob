#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ota_fixture=$(mktemp -d "${TMPDIR:-/tmp}/ota-update.XXXXXX")
trap 'rm -rf "$ota_fixture"' EXIT
mkdir -p "$ota_fixture/freertos"
for header in esp_log.h esp_ota_ops.h esp_http_client.h esp_app_desc.h esp_app_format.h freertos/FreeRTOS.h freertos/task.h; do
  printf '#include "esp_stub.h"\n' > "$ota_fixture/$header"
done
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable \
  -fsanitize=address,undefined -I"$ota_fixture" -Itests/fixtures/ota_update -Icommon \
  tests/test_ota_update.c -o "$ota_fixture/test"
"$ota_fixture/test"
