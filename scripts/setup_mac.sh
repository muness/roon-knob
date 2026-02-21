#!/bin/bash
# One-time setup for macOS development.
# Run once after cloning, or after changing IDF versions.
#
# Usage: ./scripts/setup_mac.sh
set -euo pipefail

# --- Host dependencies ---
if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew missing: install from https://brew.sh"
  exit 1
fi
brew install cmake ninja pkg-config sdl2 curl

# --- ESP-IDF target setup ---
if [[ -z "${IDF_PATH:-}" ]]; then
  echo "IDF_PATH not set. Source your ESP-IDF export.sh first:"
  echo "  . \$HOME/esp/esp-idf/export.sh"
  exit 1
fi

cd "$(dirname "$0")/../idf_app"

echo "Cleaning build environment..."
rm -f sdkconfig sdkconfig.old
idf.py fullclean

echo "Setting target to esp32s3..."
idf.py set-target esp32s3

echo ""
echo "Setup complete. Next steps:"
echo "  idf.py build flash monitor"
echo "  — or with WiFi credentials —"
echo "  SSID=MyWiFi PASS=MyPass ./scripts/install.sh"
