#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
trace_test_dir="$(mktemp -d)"
trap 'rm -rf "$trace_test_dir"' EXIT
mkdir -p "$trace_test_dir/freertos" "$trace_test_dir/lwip"
for header in esp_err.h esp_http_server.h esp_log.h esp_timer.h freertos/FreeRTOS.h lwip/sockets.h; do
    printf '#include "fake_idf.h"\n' > "$trace_test_dir/$header"
done
# Enforce the diagnostic's own frame independently of the host harness. This
# catches the old 3 KiB snapshot before it reaches the 4 KiB Wi-Fi event stack.
cc -std=c11 -O2 -Wall -Wextra -Werror -Wframe-larger-than=512 \
    -I"$trace_test_dir" -Itests/fixtures/portal_trace -Icommon \
    -c common/portal_trace.c -o "$trace_test_dir/portal_trace.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$trace_test_dir" -Itests/fixtures/portal_trace -Icommon \
    tests/test_portal_trace.c common/portal_trace.c \
    -o "$trace_test_dir/test-portal-trace"
"$trace_test_dir/test-portal-trace"
