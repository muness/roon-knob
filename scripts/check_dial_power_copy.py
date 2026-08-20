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
        "cancel if it reports the main S3",
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
        "the other to the auxiliary ESP32",
    ),
    ".github/RELEASE_TEMPLATE.md": (
        "two programmable\nprocessors",
        "both are part of the board's power budget",
        "Wi-Fi can rest while staying connected",
        "The main ESP32-S3 can nap between jobs",
        "A platform power snapshot now supplies battery level",
        "The existing Deep-sleep path now shuts down cleanly",
        "The otherwise-unused second ESP32 is parked",
        "Normal logs are quiet, but power transitions remain observable",
        "A powered Deep-sleep test leaves evidence behind",
        "source- and build-verified mechanisms, not a battery-life result",
        "a nearly full\nbattery can temporarily receive the charging policy",
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
