Roon Knob

A compact ESP32-S3-based rotary controller + LCD that acts as a Roon remote.
It displays the current track and artist, lets you change volume, play/pause, and switch zones — all over your local network.

## Overview

Roon Knob consists of three main components:

| Component | Description |
| --- | --- |
| ESP32-S3 Firmware | LVGL-based UI running on a 240×240 round LCD with rotary encoder input. Connects to Roon via a local HTTP bridge. |
| LVGL PC Simulator | SDL2 desktop build for macOS/Linux that mirrors the firmware UI for rapid development. |
| Roon Sidecar (Bridge) | Node.js service exposing `/zones`, `/now_playing`, and `/control` endpoints and advertising `_roonknob._tcp`. |

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
2. Build and run the PC simulator: `./scripts/run_pc.sh` (expects a bridge at `http://127.0.0.1:8088` by default). The simulator now uses LVGL 9 to render a 240×240 round UI (SDL2 backed): ←/→ change volume (POST `/control`), space toggles play/pause, and the status dot reflects HTTP health. Use `ROON_BRIDGE_BASE`/`ZONE_ID` to target other bridges (zone names or IDs work; it will fall back to the first discovered zone if the requested one is missing).
3. Flash the ESP32-S3 target: set `IDF_PATH` and run `./scripts/build_flash_idf.sh /dev/tty.usbmodemXYZ`.
4. Launch the bridge: `cd roon-extension && npm install && npm start`.

## Task Management

All work is tracked through Beads (see `AGENTS.md`).

```bash
bd ready
bd update bd-XX --status in_progress
bd close bd-XX --reason "done"
bd sync
```
