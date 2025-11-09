#!/usr/bin/env bash
set -euo pipefail
PORT="${PORT:-/dev/cu.wchusbserial10}"
SSID="${SSID:-YourWiFi}"
PASS="${PASS:-YourPass}"
BRIDGE_BASE="${BRIDGE_BASE:-http://192.168.1.10:8088}"
ZONE_ID="${ZONE_ID:-office}"

cd "$(dirname "$0")/../idf_app"

cat > sdkconfig.override <<EOF
CONFIG_RK_DEFAULT_SSID="${SSID}"
CONFIG_RK_DEFAULT_PASS="${PASS}"
CONFIG_RK_DEFAULT_BRIDGE_BASE="${BRIDGE_BASE}"
CONFIG_RK_DEFAULT_ZONE_ID="${ZONE_ID}"
EOF

idf.py set-target esp32s3
idf.py build
export ESPTOOL_OPEN_PORT_ATTEMPTS=0
idf.py -p "$PORT" -b 460800 flash
echo "Flashed with: SSID=$SSID BRIDGE_BASE=$BRIDGE_BASE ZONE_ID=$ZONE_ID"
