# Development Guide

## Architecture

Roon Knob has three components:

| Component | Description |
|-----------|-------------|
| **ESP32-S3 Firmware** | LVGL-based UI on a 360×360 round LCD. Polls the bridge for now-playing data and sends control commands. |
| **PC Simulator** | SDL2 desktop build that mirrors the firmware UI for rapid development without hardware. |
| **Bridge** | Node.js service that connects to Roon/HiFi systems. See [unified-hifi-control](https://github.com/cloud-atlas-ai/unified-hifi-control). |

## Repository Structure

```
roon-knob/
├── idf_app/           # ESP32-S3 firmware (ESP-IDF)
│   ├── main/          # Application code
│   ├── components/    # Custom ESP-IDF components
│   └── sdkconfig.defaults
├── common/            # Shared code between firmware and simulator
│   ├── ui.c           # LVGL UI implementation
│   ├── roon_client.c  # HTTP client for bridge API
│   └── app_main.c     # Main application logic
├── pc_sim/            # LVGL + SDL2 simulator
├── web/               # Web flasher (deployed to GitHub Pages)
├── scripts/           # Build and setup helpers
└── docs/              # Documentation
```

## PC Simulator

The simulator lets you develop UI without flashing hardware.

### Setup (macOS)

```bash
./scripts/setup_mac.sh  # Installs cmake, ninja, sdl2, curl via Homebrew
```

### Run

```bash
./scripts/run_pc.sh     # Builds and runs simulator
```

The simulator expects the bridge at `http://127.0.0.1:8088`. Run the bridge from the [unified-hifi-control](https://github.com/cloud-atlas-ai/unified-hifi-control) repo.

### Simulator Controls

| Key | Action |
|-----|--------|
| ↑/↓ or ←/→ | Volume |
| Space/Enter | Play/pause |
| Z or M | Zone picker |

## Firmware Development

### Prerequisites

1. [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) installed (typically to `~/esp/esp-idf`)
2. USB-C cable connected to the knob

### Build and Flash

```bash
export IDF_PATH=~/esp/esp-idf
source "$IDF_PATH/export.sh"

cd idf_app
idf.py build
idf.py flash -p /dev/cu.usbmodem*  # Adjust port for your system
idf.py monitor -p /dev/cu.usbmodem*  # View logs (Ctrl+] to exit)
```

Or use the helper script:

```bash
./scripts/build_flash_idf.sh /dev/cu.usbmodem101
```

**Troubleshooting:** If you get "No serial data received", retry a few times or try another cable.

### Releasing Firmware

Releases are handled via CI. Just tag and push:

```bash
git tag -a v1.2.7 -m "Release description"
git push origin v1.2.7
```

The CI builds firmware, creates a GitHub release, and deploys the web flasher.

## Bridge Development

Bridge code is at [unified-hifi-control](https://github.com/cloud-atlas-ai/unified-hifi-control).

## Hardware

The [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) includes:

- ESP32-S3 with 16MB Flash + 8MB PSRAM
- 1.8" 360×360 IPS LCD (SH8601 via QSPI)
- CST816 capacitive touch (I2C)
- Rotary encoder with push button
- Battery management (charging + voltage monitoring)
- Vibration motor

See [board overview](../esp/hw-reference/board.md) for pin mappings.

## Task Management

We use [Beads](https://github.com/anthropics/beads) for task tracking:

```bash
bd ready              # List unblocked tasks
bd update roon-knob-XX --status in_progress
bd close roon-knob-XX -r "Implemented in v1.2.6"
bd sync               # Push/pull with git
```
