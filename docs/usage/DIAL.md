# HiPhi Dial

The flagship HiPhi controller: a [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) (~$50) with a round AMOLED display, a rotary encoder, and touch.

See the [repository README](../../README.md) for the rest of the family and the bridge setup that every controller shares.

## Quick Start

> **New to this?** See the [Getting Started from Scratch](GETTING_STARTED.md) guide for detailed step-by-step instructions.

### 1. Flash the firmware (one-time)

Use the [Stable Web Flasher](https://firmware.hiphi.audio/stable/?target=dial) in a current desktop version of Chrome, Edge, or Firefox — no tools to install. iPhone, iPad, and Android cannot flash over USB. On a supported computer, plug in the Dial via USB-C and click "Flash Dial main controller".

> **Prefer command line?** See [Firmware Flashing](FIRMWARE_FLASHING.md) for esptool instructions.

Stable firmware updates are available over Wi-Fi. Beta and Alpha prereleases remain opt-in through the [Beta Web Flasher](https://firmware.hiphi.audio/beta/?target=dial) and are never pushed through the stable OTA feed.

### 2. Run the bridge

The Dial needs Unified Hi-Fi Control running somewhere on your network. See [Run the Bridge](../../README.md#run-the-bridge).

### 3. Set up the Dial

Power on the Dial. It creates a Wi-Fi network called **"hiphi-dial-setup"**. Connect to it, enter your Wi-Fi credentials, and you're done. See [WIFI_PROVISIONING.md](WIFI_PROVISIONING.md) for the full captive-portal walkthrough.

The Dial finds the bridge automatically via mDNS. It remembers the selected bridge's name and resolves its current address when reconnecting, so an IP change does not normally require editing settings. If multiple bridges are found before a selection is established, Settings asks you to choose one by entering its URL.

Dial Settings and the web configuration page share the same connection status: searching, discovered but unresolved, resolved but unavailable, connected with no zones, selected zone unavailable, or ready. Connection details show the resolution method, current address, zone freshness, next check, and last successful response. The web status refreshes without replacing unsaved form fields.

Existing discovered IP-only settings can adopt a bridge name while that IP still matches its mDNS advertisement. An already-obsolete IP with no stored name cannot safely identify its former bridge; set its URL once rather than guessing another installation. Manual URLs are preserved. See [connection recovery](../connection-recovery.md) for behavior and validation.

## Controls

| Action | What it does |
|--------|--------------|
| **Turn the knob** | Volume up/down |
| **Press the knob** | Open zone picker |
| **Tap the screen** | Play/pause |
| **Swipe up** | Art mode (hide controls, show album art) |
| **Swipe down** | Exit art mode |
| **Long-press the top of the screen (zone name or zone icon)** | Settings |

Velocity-sensitive volume control means a slow turn adjusts finely and a fast turn jumps.

## Troubleshooting

| Display shows | Meaning |
|---------------|---------|
| "WiFi: Setup Mode" | Connect to "hiphi-dial-setup" network to configure Wi-Fi |
| "Extension: Searching..." | Looking for the bridge — make sure it's running |
| "Extension: Connected" | Ready to use |

If mDNS doesn't work on your network, enter the bridge URL manually in Settings (long-press the top of the screen, on the zone name or zone icon).

## Hardware and Internals

Dial hardware reference and driver notes live in [`docs/dial/`](../dial/): [display](../dial/DISPLAY.md), [touch](../dial/TOUCH_INPUT.md), [swipe gestures](../dial/SWIPE_GESTURES.md), [rotary encoder](../dial/ROTARY_ENCODER.md), [battery](../dial/BATTERY_MONITORING.md), [fonts](../dial/FONTS.md), and [pin mappings](../dial/hw-reference/HARDWARE_PINS.md).

The Dial ships as two images: the main controller and an [auxiliary parking image](../dial/DUAL_CHIP_ARCHITECTURE.md) for the board's second ESP32.
