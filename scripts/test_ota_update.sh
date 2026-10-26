#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ota_fixture=$(mktemp -d "${TMPDIR:-/tmp}/ota-update.XXXXXX")
trap 'rm -rf "$ota_fixture"' EXIT
mkdir -p "$ota_fixture/freertos"
for header in esp_log.h esp_ota_ops.h esp_http_client.h esp_app_desc.h esp_app_format.h freertos/FreeRTOS.h freertos/task.h; do
  printf '#include "esp_stub.h"\n' > "$ota_fixture/$header"
done

# One host build per target geometry. The shared client derives what it may install from the
# running image's own project name plus the chip and flash size the firmware was built for.
run_target() {
  local project=$1 slug=$2 chip=$3 flash=$4
  shift 4
  cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable \
    -pthread -Iinclude -fsanitize=address,undefined -I"$ota_fixture" -Itests/fixtures/ota_update -Icommon \
    -DCONFIG_IDF_TARGET_"$chip"=1 -DCONFIG_ESPTOOLPY_FLASHSIZE_"$flash"=1 \
    -DOTA_TEST_PROJECT="\"$project\"" -DOTA_TEST_SLUG="\"$slug\"" "$@" \
    tests/test_ota_update.c -o "$ota_fixture/test"
  "$ota_fixture/test"
}

run_target hiphi_dial hiphi-dial ESP32S3 16MB -DOTA_TEST_LEGACY_DIAL -DOTA_TEST_CHANNEL -DOTA_TEST_ROLLBACK
run_target hiphi_frame hiphi-frame ESP32S3 16MB
run_target hiphi_halo hiphi-halo ESP32S3 16MB
run_target hiphi_rlcd_42 hiphi-rlcd ESP32S3 16MB
run_target hiphi_joy hiphi-joy ESP32S3 8MB
run_target hiphi_m5dial hiphi-dial-beta ESP32S3 8MB
run_target hiphi_sticks3 hiphi-sticks3-beta ESP32S3 8MB
run_target hiphi_stopwatch hiphi-stopwatch-beta ESP32S3 16MB
run_target hiphi_stackchan hiphi-kizz-beta ESP32S3 16MB
run_target hiphi_tough hiphi-tough ESP32 16MB
