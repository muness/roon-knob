#!/bin/bash
set -euo pipefail

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "IDF_PATH must be set"
  exit 1
fi

PORT=${1:-/dev/tty.usbmodemXYZ}
TARGET=esp32s3

pushd esp_dial >/dev/null
idf.py set-target "$TARGET"
idf.py -p "$PORT" build flash monitor
popd >/dev/null
