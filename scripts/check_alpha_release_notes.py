#!/usr/bin/env python3
"""Keep v2.7 alpha notes complete and honest about feature chronology."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NOTES = ROOT / ".github" / "RELEASE_TEMPLATE.md"

REQUIRED = (
    "# HiPhi v{{VERSION}}",
    "## The Waveshare Dial now sleeps between jobs",
    "## What changed since v2.6.0-beta.1",
    "BLE media-remote support is not new in this alpha",
    "adds the same host to\nHiPhi RLCD",
    "### Nearby Wi-Fi scanning on every controller",
    "All nine physical controllers now scan for nearby 2.4 GHz",
    "Waveshare Dial shows the list in both captive setup and connected settings",
    "one shared power snapshot now supplies",
    "16-sample ADC reading is cached for 15 seconds",
    "routine two-second now-playing request, response, and\n  parse messages",
    "Every physical controller exposes `/power-debug`",
    "Dial and Frame also provide the one-time 15-second powered Deep-sleep test",
    "## Choose your hardware and firmware",
    "ESP32-S3-Knob-Touch-LCD-1.8",
    "ESP32-S3-PhotoPainter",
    "ESP32-S3-RLCD-4.2",
    "SKU K137",
    "SKU K034",
    "SKU K130",
    "SKU K150",
    "SKU C152",
    "SKU K151",
    "K130-V11 replacement is a\ndifferent revision and is not supported",
    "not a tenth controller",
    "Alpha builds\n> are never sent through automatic OTA",
)

FIRMWARE_ASSETS = (
    "hiphi_dial_merged.bin",
    "hiphi_knob_aux_park_merged.bin",
    "hiphi_frame_merged.bin",
    "hiphi_rlcd_merged.bin",
    "hiphi_joy_merged.bin",
    "hiphi_tough_merged.bin",
    "hiphi_m5dial_merged.bin",
    "hiphi_sticks3_merged.bin",
    "hiphi_stopwatch_merged.bin",
    "hiphi_stackchan_merged.bin",
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
    for asset in FIRMWARE_ASSETS:
        if asset not in text:
            failures.append(f"missing firmware asset link {asset}")

    if failures:
        print("Alpha release-note check FAILED", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Alpha release-note check passed")
    print("- BLE chronology and target expansion are explicit")
    print("- Dial power work and its measurement limits are explicit")
    print("- all nine physical targets link to hardware and firmware")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
