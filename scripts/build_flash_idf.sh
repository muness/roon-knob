#!/bin/bash
set -euo pipefail

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "IDF_PATH must be set"
  exit 1
fi

PORT=${1:-/dev/tty.usbmodemXYZ}
TARGET=esp32s3

pushd idf_app >/dev/null
# Only set-target if not already configured (avoids unnecessary fullclean)
if [ ! -f build/CMakeCache.txt ] || ! grep -q "IDF_TARGET:STRING=$TARGET" build/CMakeCache.txt 2>/dev/null; then
    idf.py set-target "$TARGET"
fi
idf.py -p "$PORT" build flash monitor
popd >/dev/null
