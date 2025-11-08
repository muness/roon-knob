Roon Knob

A compact ESP32-S3-based rotary controller + LCD that acts as a Roon remote.
It displays the current track and artist, lets you change volume, play/pause, and switch zones — all over your local network.

Overview

Roon Knob consists of three main components:

Component	Description
ESP32-S3 Firmware	LVGL-based UI running on a 240×240 round LCD with rotary encoder input. Connects to Roon via local HTTP bridge.
LVGL PC Simulator	SDL2 desktop build for macOS/Linux that mirrors the firmware UI for rapid development.
Roon Sidecar (Bridge)	Lightweight Node.js service that exposes /zones, /now_playing, and /control endpoints via Roon’s extension API and advertises itself over mDNS.


Repository Structure

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
Setup

Task Management

All development tasks are tracked with Beads￼ (bd).

bd ready        # show active issues
bd create "Add OTA" -t feature -p 1
bd update bd-12 --status in_progress
bd close bd-12 --reason "done"
bd sync

See AGENTS.md￼ for details on using Beads in this project.
