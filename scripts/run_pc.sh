#!/bin/bash
set -euo pipefail

ROON_BRIDGE_BASE=${ROON_BRIDGE_BASE:-http://192.168.1.213:8088}
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
