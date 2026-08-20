#!/usr/bin/env python3
"""Keep Kizz user-facing while preserving StackChan compatibility identifiers."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

REQUIRED = {
    "web/manifest-stackchan.json": [
        '"name": "Kizz Playback Companion (Alpha)"',
        '"path": "hiphi_stackchan.bin"',
    ],
    "web/flash.html": [
        "Kizz Playback Companion",
        "Flash Kizz Alpha",
        "stackchan: 'Kizz'",
        'manifest="manifest-stackchan.json"',
    ],
    "web/flash-pr.html": [
        "Kizz Playback Companion",
        "Flash Kizz Alpha",
        "stackchan: 'Kizz'",
    ],
    ".github/RELEASE_TEMPLATE.md": [
        "Kizz Playback Companion",
        "hiphi_stackchan_merged.bin",
    ],
    ".github/workflows/docker.yml": [
        'prepare_m5_beta stackchan stackchan "Kizz Playback Companion"',
        "Flash Kizz Companion Alpha",
    ],
    "m5_beta_app/main/CMakeLists.txt": ['set(_slug "hiphi-kizz-beta")'],
    "m5_beta_app/sdkconfig.stackchan.defaults": [
        'CONFIG_LWIP_LOCAL_HOSTNAME="hiphi-kizz-beta"',
        "CONFIG_M5_PLATFORM_EXPECT_STACKCHAN=y",
    ],
    "components/m5_platform/m5_platform.cpp": [
        's_board == M5_PLATFORM_BOARD_STACKCHAN ? "Kizz"',
        "board_M5StackChan",
    ],
}

FORBIDDEN = {
    "web/manifest-stackchan.json": ["StackChan Playback Companion"],
    "web/flash.html": ["StackChan Playback Companion", "Flash StackChan"],
    "web/flash-pr.html": ["StackChan Playback Companion", "Flash StackChan"],
    ".github/RELEASE_TEMPLATE.md": ["StackChan Playback Companion"],
    ".github/workflows/docker.yml": [
        'prepare_m5_beta stackchan stackchan "StackChan Playback Companion"',
        "Flash StackChan Companion Alpha",
    ],
    "m5_beta_app/main/CMakeLists.txt": ["hiphi-stackchan-beta"],
    "m5_beta_app/sdkconfig.stackchan.defaults": ["hiphi-stackchan-beta"],
}


def main() -> int:
    errors: list[str] = []
    for relative, needles in REQUIRED.items():
        path = ROOT / relative
        if not path.is_file():
            errors.append(f"{relative}: required file is missing")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                errors.append(f"{relative}: missing required identity {needle!r}")

    for relative, needles in FORBIDDEN.items():
        path = ROOT / relative
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle in text:
                errors.append(f"{relative}: old product identity remains: {needle!r}")

    if errors:
        print("Kizz identity check FAILED", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print("Kizz identity check passed")
    print("- user-facing product name: Kizz")
    print("- compatibility target and asset stem: stackchan")
    print("- exact hardware/API identity: M5StackChan")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
