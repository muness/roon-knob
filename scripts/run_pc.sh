#!/bin/bash
set -euo pipefail

if [[ -z "${ROON_BRIDGE_BASE:-}" ]]; then
    echo "Error: ROON_BRIDGE_BASE environment variable not set"
    echo "Usage: ROON_BRIDGE_BASE=http://your-bridge-ip:8088 $0"
    exit 1
fi
export ROON_BRIDGE_BASE

mkdir -p build/pc_sim
cmake -S pc_sim -B build/pc_sim -G Ninja
cmake --build build/pc_sim
BUILD_BIN=build/pc_sim/roon_knob_pc
if [[ ! -x "$BUILD_BIN" ]]; then
  echo "expected binary $BUILD_BIN"
  exit 1
fi
"$BUILD_BIN"
