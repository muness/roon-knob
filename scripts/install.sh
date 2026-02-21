#!/usr/bin/env bash
# Build and flash with baked-in WiFi credentials.
# Run setup_mac.sh first if this is a fresh clone.
#
# Usage: SSID=MyWiFi PASS=MyPass BRIDGE_BASE=http://x:8088 ./scripts/install.sh
set -euo pipefail
PORT="${PORT:-/dev/cu.usbmodem101}"
SSID="${SSID:-YourWiFi}"
PASS="${PASS:-YourPass}"
BRIDGE_BASE="${BRIDGE_BASE:-http://192.168.1.2:8088}"

cd "$(dirname "$0")/../idf_app"

# Bail early if setup hasn't been run
if [ ! -f build/CMakeCache.txt ]; then
    echo "No build directory found. Run setup first:"
    echo "  ./scripts/setup_mac.sh"
    exit 1
fi

# Write credentials to git-ignored sdkconfig.local
cat > sdkconfig.local <<EOF
CONFIG_RK_DEFAULT_SSID="${SSID}"
CONFIG_RK_DEFAULT_PASS="${PASS}"
CONFIG_RK_DEFAULT_BRIDGE_BASE="${BRIDGE_BASE}"
EOF
echo "Updated sdkconfig.local: SSID=$SSID BRIDGE_BASE=$BRIDGE_BASE"

# Regenerate sdkconfig with credentials overlay
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.local"
rm -f sdkconfig
idf.py reconfigure

idf.py build
idf.py -p "$PORT" -b 921600 flash
echo "Flashed with: SSID=$SSID BRIDGE_BASE=$BRIDGE_BASE"
