Roon Knob

A compact ESP32-S3-based rotary controller + LCD that acts as a Roon remote. [ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8)

It displays the current track and artist, lets you change volume, play/pause, and switch zones — all over your local network.

## Overview

Roon Knob consists of three main components:

| Component | Description |
| --- | --- |
| ESP32-S3 Firmware | LVGL-based UI running on a 360×360 round LCD with rotary encoder input. Connects to Roon via a local HTTP bridge. |
| LVGL PC Simulator | SDL2 desktop build for macOS/Linux that mirrors the firmware UI for rapid development. |
| Roon Sidecar (Bridge) | Node.js service exposing `/zones`, `/now_playing`, and `/control` endpoints and advertising `_roonknob._tcp`. |

Hardware notes: this Waveshare board pairs an ESP32‑S3 (with 16 MB Flash + 8 MB PSRAM) and an ESP32 co‑processor, drives a 1.8" 360×360 IPS LCD (ST77916 over QSPI), and uses a CST816 capacitive touch controller over I2C with INT. It also includes a rotary encoder (push + A/B), mic + audio decoder, vibration motor, TF slot, and battery charge management. See [board.md](docs/reference/hardware/board.md).

The ESP32-S3 knob in this project has 16 MB of flash and 8 MB of PSRAM, so there is no need to shrink or adjust the partition table for space. The firmware can safely use the existing “factory + OTA0 + OTA1 + nvs + spiffs” layout. Do not change sdkconfig to a smaller table or add space-saving flags: there is plenty of room for both the app and future features.


## Repository Structure

```
roon-knob/
├── idf_app/           # ESP32-S3 firmware (ESP-IDF)
│   ├── main/
│   ├── partitions.csv
│   └── sdkconfig.defaults
├── pc_sim/            # LVGL + SDL2 simulator for macOS/Linux
├── roon-extension/    # Node.js sidecar bridge
├── scripts/           # Setup and build helpers
├── history/           # Ephemeral design + planning notes
└── .beads/            # Task tracker database (Beads)
```

## User Setup Guide

> **Note:** This guide assumes comfort with command line and Docker. The bridge requires an always-on computer on your network.

### Prerequisites

- **Roon Core** running on your network
- **Roon Knob hardware** (Waveshare ESP32-S3-Knob-Touch-LCD-1.8)
- **Docker** installed on a computer that stays on (NAS, Raspberry Pi, server, etc.)

### Step 1: Start the Bridge

Create a `docker-compose.yml` file (or use the one in `roon-extension/`):

```yaml
services:
  roon-knob-bridge:
    image: docker.io/muness/roon-extension-knob:latest
    restart: unless-stopped
    network_mode: host
    volumes:
      - roon-knob-bridge-data:/home/node/app/data

volumes:
  roon-knob-bridge-data:
```

Then start it:

```bash
docker compose up -d
docker compose logs -f  # Watch logs (Ctrl+C to exit)
```

You'll see: `Waiting for Roon Core...`

### Step 2: Authorize in Roon

1. Open **Roon** → **Settings** → **Extensions**
2. Find **"Roon Knob Bridge"** and click **Enable**
3. The bridge will log: `Roon Core paired!`

With `restart: unless-stopped`, Docker will keep the bridge running across reboots.

### Step 3: Power On the Knob

1. Connect the knob via USB-C (or battery)
2. It will show **"WiFi: Setup Mode"** and create a network called **"roon-knob-setup"**
3. Connect your phone/laptop to that network
4. A setup page should auto-popup (or go to `http://192.168.4.1`)
5. Enter your WiFi credentials and click **Connect**

### Step 4: Select a Zone

After connecting to WiFi:
1. The knob will show **"Press knob to select zone"**
2. Press the rotary encoder to open the zone picker
3. Turn the knob to scroll through zones, press to select

### Using the Knob

| Control | Action |
|---------|--------|
| **Turn knob** | Adjust volume |
| **Press knob** | Open zone picker |
| **Tap screen** | Play/pause |

### Troubleshooting

| Status | Meaning |
|--------|---------|
| "WiFi: Connecting..." | Connecting to your network |
| "WiFi: Retrying..." | Wrong credentials or network issue |
| "WiFi: Setup Mode" | Enter WiFi credentials via captive portal |
| "Bridge: Searching..." | Looking for bridge via mDNS |
| "Bridge: Unreachable" | Bridge not running or wrong URL |
| "Bridge: Connected" | Ready to use! |

If mDNS discovery fails, you can manually enter the bridge URL (e.g., `http://192.168.1.100:8088`) in the WiFi setup page or in Settings.

---

## Development

### PC Simulator

Build and run the simulator for rapid UI development:

```bash
./scripts/setup_mac.sh  # Install dependencies (cmake, ninja, sdl2, curl)
./scripts/run_pc.sh     # Run simulator (expects bridge at http://127.0.0.1:8088)
```

Controls:
- **↑/↓ or ←/→**: Volume
- **Space/Enter**: Play/pause
- **Z or M**: Zone picker

### Flashing the Firmware

1. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) (typically to `~/esp/esp-idf`)

2. Connect the knob via USB-C and identify the port:
   - **macOS**: `ls /dev/cu.*` → look for `usbmodem*` or `wchusbserial*`
   - **Linux**: `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`

3. Build and flash:
   ```bash
   export IDF_PATH=~/esp/esp-idf
   source "$IDF_PATH/export.sh"
   ./scripts/build_flash_idf.sh /dev/cu.usbmodem101
   ```

4. Monitor serial output:
   ```bash
   idf.py monitor -p /dev/cu.usbmodem101  # Exit with Ctrl+]
   ```

### Bridge Development

```bash
cd roon-extension
npm install
npm run dev  # Hot reload mode
```

## Roadmap

### Release Blockers

These must be completed before the device can be used by non-developers:

| Feature | Status | Description |
|---------|--------|-------------|
| **WiFi Provisioning (SoftAP)** | Complete | Device spawns AP "roon-knob-setup" after 5 failed STA attempts or if no SSID configured. True captive portal with DNS hijacking for auto-popup on phones. |
| **mDNS-first Bridge Discovery** | Complete | mDNS `_roonknob._tcp` discovery is now the primary method. Compile-time default used only as fallback. Captive portal allows manual bridge URL entry. |
| **First-Boot Status UI** | Complete | Meaningful status messages: "WiFi: Connecting/Retrying/Setup Mode", "Bridge: Searching/Found/Connected/Offline". Clear feedback for all connection states. |
| **Bridge Connectivity Test** | Complete | URL format validation in captive portal. Auto-test on WiFi connect with feedback. "Test Bridge" button in settings for manual testing. |

### Post-Release Improvements

Nice to have but not blocking initial release:

| Feature | Priority | Description |
|---------|----------|-------------|
| Persistent Error Log | Medium | Store last 100 errors in SPIFFS, accessible via serial or diagnostic menu |
| Manual Refresh Button | Medium | Force re-discovery of zones and bridge without restart |
| Watchdog Timer | Low | Auto-reboot if polling thread deadlocks |
| OTA Rollback | Low | Revert to previous firmware if new version fails to boot |
| QR Code Setup | Low | Display QR code during setup pointing to docs or config |
| BLE Provisioning | Low | Alternative to SoftAP for WiFi credential entry |

### Current Limitations

- No onboarding wizard on first boot
- Error messages don't distinguish WiFi vs bridge vs zone issues

## Task Management

All work is tracked through Beads (see `AGENTS.md`). Roadmap items above are tracked as beads tasks.

```bash
bd ready              # list unblocked tasks
bd update bd-XX --status in_progress
bd close bd-XX --reason "done"
bd sync
```
