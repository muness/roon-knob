#!/usr/bin/env python3
"""Keep Dial power claims complete, actionable, and measurement-honest."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

REQUIRED = {
    "web/flash.html": (
        "Battery work in this alpha",
        "Wi-Fi modem sleep",
        "automatic Light-sleep",
        "cached 16-sample ADC reading",
        "power-debug view",
        "turning the encoder wakes",
        "one-time parking image",
        "have not been measured yet",
        "the other to the auxiliary ESP32",
        "Cancel if it identifies an ESP32-S3",
    ),
    "web/flash-pr.html": (
        "Battery work in this alpha",
        "Wi-Fi modem sleep",
        "automatic Light-sleep",
        "cached 16-sample ADC reading",
        "power-debug view",
        "turning the encoder wakes",
        "one-time parking image",
        "have not been measured yet",
        "Do this once for the lowest idle draw",
        "Cancel if it identifies an ESP32-S3",
        "the other to the auxiliary ESP32",
    ),
    ".github/RELEASE_TEMPLATE.md": (
        "two programmable\nprocessors",
        "Wi-Fi modem sleep",
        "Automatic Light-sleep",
        "Main ESP32-S3 Deep-sleep",
        "temporary timer wake source",
        "The second ESP32 is parked",
        "one shared power snapshot now supplies",
        "16-sample ADC reading is cached for 15 seconds",
        "Less log noise",
        "one-time 15-second test",
        "firmware evidence, not an ammeter",
        "We have not measured a battery-life improvement yet",
        "A nearly full battery can temporarily receive the plugged-in",
        "otherwise-unused auxiliary ESP32 once",
    ),
    ".github/workflows/docker.yml": (
        "build-knob-aux, build-ble-off",
        "Download Waveshare Dial auxiliary parking firmware",
        "manifest-knob-aux-${PREVIEW_ID}.json",
        "Flash HiPhi Dial + one-time auxiliary power step",
    ),
}


def main() -> int:
    failures: list[str] = []
    for relative, needles in REQUIRED.items():
        path = ROOT / relative
        if not path.is_file():
            failures.append(f"{relative}: required file is missing")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                failures.append(f"{relative}: missing required power copy {needle!r}")

    if failures:
        print("Dial power copy check FAILED", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Dial power copy check passed")
    print("- main and auxiliary processor changes are both explained")
    print("- connected idle, Deep-sleep, wake, and measurement limits are explicit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
