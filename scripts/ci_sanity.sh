#!/usr/bin/env bash
set -euo pipefail

if [[ -d pc_sim ]]; then
  cmake -S pc_sim -B build_pc_ci
  cmake --build build_pc_ci
else
  echo "Skipping pc_sim build: pc_sim/ is not present in this checkout."
fi

if [[ -n "${IDF_PATH:-}" ]] && command -v idf.py >/dev/null 2>&1; then
  pushd idf_app >/dev/null
  idf.py build
  popd >/dev/null
else
  echo "Skipping idf_app build: ESP-IDF is not loaded in this environment."
fi

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
