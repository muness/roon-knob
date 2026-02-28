#!/usr/bin/env bash
set -euo pipefail

cmake -S pc_sim -B build_pc_ci
cmake --build build_pc_ci

pushd esp_dial >/dev/null
idf.py build
popd >/dev/null

WARNINGS=$(rg -n --glob '*.[ch]' "#include <esp_" common || true)
if [[ -n "$WARNINGS" ]]; then
  echo "esp_ includes found in common/" >&2
  echo "$WARNINGS" >&2
  exit 1
fi

WARNINGS=$(rg -n --glob '*.[ch]' "SDL_" common || true)
if [[ -n "$WARNINGS" ]]; then
  echo "SDL_ symbols found in common/" >&2
  echo "$WARNINGS" >&2
  exit 1
fi

WARNINGS=$(rg -n --glob '*.[ch]' "curl_" common || true)
if [[ -n "$WARNINGS" ]]; then
  echo "curl_ symbols found in common/" >&2
  echo "$WARNINGS" >&2
  exit 1
fi
