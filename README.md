# Roon Knob

Custom firmware and a Roon extension that turn a [Waveshare ESP32-S3 Knob](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) into a dedicated Roon controller.

See what's playing, adjust volume, skip tracks, and switch zones—all from a physical knob on your desk.

[![Roon Knob demo](docs/images/roon-knob-photo.jpg)](https://photos.app.goo.gl/s5LpWqBTaXRihmjh7)
*Click to watch video demo*

## What You Need

1. **Hardware**: [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) (~$50)
2. **Roon Core** running on your network
3. **Docker host** (NAS, Raspberry Pi, always-on computer) to run the Roon extension

## Quick Start

### 1. Flash the Firmware (one-time)

Download `roon_knob.bin` from the [latest release](https://github.com/muness/roon-knob/releases/latest) and flash it to your knob:

```bash
pip install esptool
esptool.py --chip esp32s3 -p /dev/tty.usbmodem101 -b 460800 \
  --before default-reset --after hard-reset \
  write_flash 0x10000 roon_knob.bin
```

(Adjust the port for your system—see release notes for details.)

After this initial flash, future updates happen automatically over WiFi.

### 2. Run the Roon Extension

The extension connects Roon to your knob. On a Docker host (NAS, Raspberry Pi, etc.):

```yaml
# docker-compose.yml
services:
  roon-knob-bridge:
    image: muness/roon-extension-knob:latest
    restart: unless-stopped
    network_mode: host
    volumes:
      - roon-knob-bridge-data:/home/node/app/data

volumes:
  roon-knob-bridge-data:
```

> **Image tags:** Use `latest` for stable releases, or `edge` for bleeding-edge builds from master.

```bash
docker compose up -d
```

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
- Volume control with dB display
- Multi-zone support
- Automatic display dimming and sleep
- Over-the-air firmware updates
- WiFi setup via captive portal

## Troubleshooting

| Display shows | Meaning |
|---------------|---------|
| "WiFi: Setup Mode" | Connect to "roon-knob-setup" network to configure WiFi |
| "Extension: Searching..." | Looking for the extension—make sure it's running |
| "Extension: Connected" | Ready to use |

If mDNS doesn't work on your network, enter the extension URL manually in Settings (long-press zone name).

## Development

See [DEVELOPMENT.md](docs/DEVELOPMENT.md) for building firmware, running the PC simulator, and contributing.

## Support

Questions or issues? [Open an issue](https://github.com/muness/roon-knob/issues), join the [Roon Community discussion](https://community.roonlabs.com/t/50-diy-roon-desk-controller/311363), or [buy me a coffee](https://www.buymeacoffee.com/muness).
