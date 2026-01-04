# Roon Knob

Custom firmware and a companion service that turn a [Waveshare ESP32-S3 Knob](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) into a dedicated hi-fi controller.

See what's playing, adjust volume, skip tracks, and switch zones—all from a physical knob on your desk.

**Now supports multiple music sources:**
- **Roon** - Full zone control with album artwork
- **Lyrion Music Server (LMS)** - Squeezebox/LMS player control
- **OpenHome/UPnP** - Control OpenHome-compatible renderers

[![Roon Knob demo](docs/images/roon-knob-photo.jpg)](https://photos.app.goo.gl/s5LpWqBTaXRihmjh7)
*Click to watch video demo*

## What You Need

1. **Hardware**: [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) (~$50) - buy it at [amazon](https://amzn.to/4pYZdiC) to support my work.
2. **Music source**: Roon Core, Lyrion Music Server, or OpenHome renderer on your network
3. **Docker host** (NAS, Raspberry Pi, always-on computer) to run the control service

## Quick Start

> **New to this?** See the [Getting Started from Scratch](docs/usage/GETTING_STARTED.md) guide for detailed step-by-step instructions.

### 1. Flash the Firmware (one-time)

Use the [Web Flasher](https://roon-knob.muness.com/flash.html) in Chrome or Edge—no tools to install. Just plug in the knob via USB-C and click "Flash ESP32-S3".

> **Prefer command line?** See [Firmware Flashing](docs/usage/FIRMWARE_FLASHING.md) for esptool instructions.

After flashing, future updates happen automatically over WiFi.

### 2. Run the Control Service

The control service (Unified Hi-Fi Control) connects your music source to the knob.

**Docker (recommended)**

On any Docker host (NAS, Raspberry Pi, etc.):

```yaml
# docker-compose.yml
services:
  unified-hifi-control:
    image: muness/unified-hifi-control:latest
    restart: unless-stopped
    network_mode: host
    volumes:
      - unified-hifi-control-data:/home/node/app/data

volumes:
  unified-hifi-control-data:
```

```bash
docker compose up -d
```

> **Note:** The legacy image name `muness/roon-extension-knob` still works and receives the same updates.

**Already have the [Roon Extension Manager](https://github.com/TheAppgineer/roon-extension-manager)?**

Find "Roon Knob" in the extension list and install it from there (Roon-only mode).

### 3. Authorize in Roon

Go to **Roon → Settings → Extensions** and enable **"Roon Knob Bridge"**.

### 4. Set Up the Knob

Power on the knob. It creates a WiFi network called **"roon-knob-setup"**. Connect to it, enter your WiFi credentials, and you're done.

The knob finds the extension automatically via mDNS.

## Controls

| Action | What it does |
|--------|--------------|
| **Turn the knob** | Volume up/down |
| **Press the knob** | Open zone picker |
| **Tap the screen** | Play/pause |
| **Swipe up** | Art mode (hide controls, show album art) |
| **Swipe down** | Exit art mode |
| **Long-press zone name** | Settings |

## Features

- Real-time now playing with album artwork
- Velocity-sensitive volume control (turn slow for fine adjustment, fast for quick jumps)
- Multi-zone support
- Automatic display dimming and sleep
- Over-the-air firmware updates
- WiFi setup via captive portal
- Bluetooth mode for non-Roon devices *(alpha)*

## Bluetooth Mode *(alpha)*

> **Note:** Due to a regression in UART communication between the two chips, Bluetooth mode is temporarily hidden from the zone picker. Access it via **Settings** (long-press the zone name) → **Bluetooth Mode *alpha***.

When you're away from your Roon setup, the knob can control any Bluetooth audio device (phone, DAP, etc.).

**To enter Bluetooth mode:**
1. Long-press the zone name to open Settings
2. Tap "Bluetooth Mode *alpha"
3. Pair your phone/DAP with two Bluetooth devices:
   - **"Knob control"** (BLE HID) - for sending commands to your device
   - **"Knob info"** (Classic BT) - for receiving track metadata from your device

**Controls in Bluetooth mode:**
- Turn knob: Volume up/down
- Tap play/pause button: Play/pause
- Tap prev/next buttons: Previous/next track

**What you'll see:**
- With both connections: Full control + track info (title, artist, album, progress)
- With "Knob control" only: Full control, but no track metadata
- With "Knob info" only: Track info displayed, but controls won't work

**To exit Bluetooth mode:**
- Press the knob and confirm "Exit Bluetooth"

**Compatibility notes:**
- Some devices (especially DAPs) can only connect to one Bluetooth profile at a time. In that case, connect "Knob control" for controls and skip "Knob info".
- The AVRCP implementation is controller-only (no audio sink), which may not work with all devices. This is why metadata is an optional second connection.
- WiFi is disabled in Bluetooth mode to save power.

## Troubleshooting

| Display shows | Meaning |
|---------------|---------|
| "WiFi: Setup Mode" | Connect to "roon-knob-setup" network to configure WiFi |
| "Extension: Searching..." | Looking for the extension—make sure it's running |
| "Extension: Connected" | Ready to use |

If mDNS doesn't work on your network, enter the extension URL manually in Settings (long-press zone name).

## Development

See [DEVELOPMENT.md](docs/dev/DEVELOPMENT.md) for building firmware, running the PC simulator, and contributing.

## Roadmap

See [PROJECT_AIMS.md](docs/meta/PROJECT_AIMS.md) for project goals and decision framework, and [ROADMAP_IDEAS.md](docs/meta/ROADMAP_IDEAS.md) for user feedback and planned improvements.

## Support

Questions or issues? [Open an issue](https://github.com/muness/roon-knob/issues), join the [Roon Community discussion](https://community.roonlabs.com/t/50-diy-roon-desk-controller/311363), or [buy me a coffee](https://www.buymeacoffee.com/muness).

## Acknowledgments

Thanks to **gTunes** from the Roon community for alpha testing, detailed feedback, and help with the velocity-sensitive volume control implementation.
