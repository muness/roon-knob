#!/usr/bin/env python3
"""Keep v2.7 alpha notes complete and honest about feature chronology."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NOTES = ROOT / ".github" / "RELEASE_TEMPLATE.md"

REQUIRED = (
    "## Highlights since v2.6.0-beta.1",
    "BLE media-remote support is **not new in this alpha**",
    "adds it to HiPhi RLCD",
    "without deleting its enabled preference, remembered peer, or\n  bond",
    "### Nearby Wi-Fi scanning during setup",
    "non-blocking scans for visible 2.4 GHz",
    "The classic Waveshare\nHiPhi Dial captive page still uses manual SSID entry",
    "## HiPhi Dial power-saving changes",
    "## Supported hardware and where to buy it",
    "ESP32-S3-Knob-Touch-LCD-1.8",
    "ESP32-S3-PhotoPainter",
    "ESP32-S3-RLCD-4.2",
    "SKU K137",
    "SKU K034",
    "SKU K130",
    "SKU K150",
    "SKU C152",
    "SKU K151",
    "K130-V11 is a\ndifferent revision and has not been qualified",
    "not a tenth supported device",
)

OFFICIAL_URLS = (
    "https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm",
    "https://www.waveshare.com/product/esp32-s3-photopainter.htm",
    "https://www.waveshare.com/esp32-s3-rlcd-4.2.htm",
    "https://shop.m5stack.com/products/atom-joystick-with-m5atoms3",
    "https://shop.m5stack.com/products/m5stack-tough-esp32-iot-development-board-kit",
    "https://shop.m5stack.com/products/m5stack-dial-esp32-s3-smart-rotary-knob-w-1-28-round-touch-screen",
    "https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit",
    "https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3",
    "https://shop.m5stack.com/products/stackchan-kawaii-co-created-open-source-ai-desktop-robot",
)


def main() -> int:
    text = NOTES.read_text(encoding="utf-8")
    failures: list[str] = []
    for needle in REQUIRED:
        if needle not in text:
            failures.append(f"missing release-note claim {needle!r}")
    for url in OFFICIAL_URLS:
        if url not in text:
            failures.append(f"missing official hardware URL {url}")

    if failures:
        print("Alpha release-note check FAILED", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Alpha release-note check passed")
    print("- BLE chronology and target expansion are explicit")
    print("- Wi-Fi scanning is scoped to the surfaces that expose it")
    print("- all nine physical targets link to exact official hardware")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
