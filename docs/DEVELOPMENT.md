# Development Guide

## Architecture

Roon Knob has three components:

| Component | Description |
|-----------|-------------|
| **ESP32-S3 Firmware** | LVGL-based UI on a 360×360 round LCD. Polls the bridge for now-playing data and sends control commands. |
| **PC Simulator** | SDL2 desktop build that mirrors the firmware UI for rapid development without hardware. |
| **Bridge** | Node.js service that connects to Roon and exposes HTTP endpoints (`/zones`, `/now_playing`, `/control`). Advertises via mDNS (`_roonknob._tcp`). |

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
├── roon-extension/    # Node.js bridge
│   └── firmware/      # OTA firmware served by bridge
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

The simulator expects the bridge at `http://127.0.0.1:8088`. Start the bridge first:

```bash
cd roon-extension && npm install && npm run dev
```

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

### Releasing Firmware

```bash
./scripts/release_firmware.sh 1.2.7  # Bumps version, builds, copies to roon-extension/firmware/
```

Then rebuild and push the Docker image to make the update available via OTA.

## Bridge Development

```bash
cd roon-extension
npm install
npm run dev  # Hot reload mode
```

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/zones` | GET | List all Roon zones |
| `/now_playing?zone_id=X` | GET | Current track info |
| `/now_playing/image?zone_id=X` | GET | Album artwork (JPEG) |
| `/control` | POST | Send commands (play, pause, next, previous, volume) |
| `/firmware/version` | GET | Latest firmware version info |
| `/firmware/download` | GET | Download firmware binary |

### Docker Build

```bash
cd ../roon-extension-generator
./build-versioned.sh knob
./publish-versioned.sh knob
./create-manifest.sh knob
```

## Hardware

The [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) includes:

- ESP32-S3 with 16MB Flash + 8MB PSRAM
- 1.8" 360×360 IPS LCD (SH8601 via QSPI)
- CST816 capacitive touch (I2C)
- Rotary encoder with push button
- Battery management (charging + voltage monitoring)
- Vibration motor

See [docs/reference/hardware/board.md](reference/hardware/board.md) for pin mappings.

## Task Management

We use [Beads](https://github.com/anthropics/beads) for task tracking:

```bash
bd ready              # List unblocked tasks
bd update roon-knob-XX --status in_progress
bd close roon-knob-XX -r "Implemented in v1.2.6"
bd sync               # Push/pull with git
```
