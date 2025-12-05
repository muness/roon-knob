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

## Getting Started

1. Install the simulator dependencies (`cmake`, `ninja`, `sdl2`, `curl`) with `./scripts/setup_mac.sh`.
2. Build and run the PC simulator: `./scripts/run_pc.sh` (expects a bridge at `http://127.0.0.1:8088` by default). The simulator uses LVGL 9 to render a 240×240 round UI (SDL2):
   - **Main screen:**
     - ↑/↓ or ←/→: Volume control (rotary knob)
     - Space/Enter: Play/pause (touch)
     - Z or M: Open zone picker
   - **Zone picker:**
     - ↑/↓: Scroll through zones
     - Space/Enter: Select zone
     - Z or M: Close without selecting
   - Status dot shows connection health
   Use `ROON_BRIDGE_BASE` env vars to target specific bridges/zones.
3. Install ESP-IDF
   - Follow the [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html)
   - Typical installation: `~/esp/esp-idf`
4. Flash the ESP32-S3 target:
   - Setup environment and build:
   - Plug the knob into your Mac/Linux workstation with a USB-C cable. If the board enumerates as a non-ESP device (no `/dev/cu.*esp*` entry, or the USB-C socket feels loose), flip the cable or try the other USB-C port — the board can revert to a USB-to-serial bootloader mode that looks like a generic `wchusbserial` device.
   - Identify the serial port:
     - **macOS**: run `ls /dev/cu.*` and look for the `wchusbserial*` or `usbmodem*` entry (e.g., `/dev/cu.usbmodem101`).
     - **Linux**: run `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`.
   - Setup ESP-IDF environment and build:

     ```bash
     export IDF_PATH=~/esp/esp-idf
     source "$IDF_PATH/export.sh"
     ./scripts/build_flash_idf.sh /dev/tty.usbmodem101
     ```

    The script forces `idf.py set-target esp32s3`, builds, flashes, and drops you into the monitor. If the chip you are actually flashing identifies as plain ESP32 (and the host reports `ESP32, not ESP32-S3`), rerun `idf.py set-target esp32` (or edit `sdkconfig`/`sdkconfig.overrides` to the esp32 target) and flash again with `idf.py` directly so the build/flash toolchain matches the silicon. If the knob is still in bootloader mode and keeps logging "ESP32" despite using the S3 target, unplug/replug while holding/resetting the board to force the S3 USB controller to enumerate correctly.
     If `idf.py` complains about `No module named 'click'` or similar, rerun `source $IDF_PATH/export.sh` in that shell and reinstall the ESP-IDF Python requirements (`python -m pip install -r "$IDF_PATH/requirements.txt"` while the export script is active) so the helper script can find `click`.

   - To monitor serial output after flashing:

     ```bash
     idf.py monitor -p /dev/cu.usbmodem101
     ```

     **Note:** To exit the monitor, press `Ctrl+]` (not Ctrl+C). This is easy to forget!
5. Launch the bridge: `cd roon-extension && npm install && npm start` (or `npm run dev` for hot reload).

## Task Management

All work is tracked through Beads (see `AGENTS.md`).

```bash
bd ready
bd update bd-XX --status in_progress
bd close bd-XX --reason "done"
bd sync
```
