#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
http_fixture=$(mktemp -d "${TMPDIR:-/tmp}/platform-http.XXXXXX")
trap 'rm -rf "$http_fixture"' EXIT
for header in esp_log.h esp_http_client.h esp_heap_caps.h esp_mac.h esp_app_desc.h miniz.h; do
  printf '#include "esp_stub.h"\n' > "$http_fixture/$header"
done
for fallback in 0 1; do
  cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable \
    -fsanitize=address,undefined -I"$http_fixture" -Itests/fixtures/platform_http -Icommon \
    -DPLATFORM_HTTP_JSON_INTERNAL_FALLBACK="$fallback" \
    -DPLATFORM_HTTP_IMAGE_REQUEST_GZIP="$((1-fallback))" \
    tests/test_platform_http_esp.c -lz -o "$http_fixture/test"
  "$http_fixture/test"
done
python3 tests/test_shared_http_composition.py
