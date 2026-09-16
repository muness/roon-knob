#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=$(mktemp "${TMPDIR:-/tmp}/zone-label-policy.XXXXXX")
trap 'rm -f "$binary"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror \
  -Icommon tests/test_zone_label_policy.c common/zone_label_policy.c \
  -o "$binary"
"$binary"
