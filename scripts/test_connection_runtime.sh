#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=$(mktemp "${TMPDIR:-/tmp}/connection-runtime.XXXXXX")
trap 'rm -f "$binary"' EXIT
link_flags=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then link_flags=(-Wl,-dead_strip); fi
if [[ -n ${IDF_PATH:-} ]]; then
  cc -Wno-deprecated-declarations -c "$IDF_PATH/components/json/cJSON/cJSON.c" -o "$binary.json.o"
  trap 'rm -f "$binary" "$binary.json.o"' EXIT
  json_flags=(-I"$IDF_PATH/components/json/cJSON" "$binary.json.o")
else
  read -r -a json_flags <<< "$(pkg-config --cflags --libs libcjson)"
fi
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -Icommon -Iinclude tests/test_connection_runtime.c \
  common/controller_connection.c common/bridge_command_plan.c common/platform/platform_mdns_endpoint.c \
  "${json_flags[@]}" "${link_flags[@]}" -o "$binary"
"$binary"
