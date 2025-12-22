# v1.3.0 - Bluetooth Mode via Dual-Chip Architecture

Major release adding Bluetooth media control as an alternative to WiFi/Roon control.

## Highlights

The Roon Knob can now control **any Bluetooth-enabled device** - not just Roon. Select "Bluetooth" from the zone picker and pair with your phone, tablet, or DAP for standalone media control.

### How It Works

The Waveshare board contains two ESP32 chips. This release leverages both:
- **ESP32-S3**: Display, touch, WiFi, mode switching
- **ESP32**: Bluetooth (BLE HID for controls, AVRCP for metadata)

The chips communicate via a 1Mbps UART protocol with CRC verification and heartbeat monitoring.

## Features

### Controls (BLE HID) - Works with ALL devices
- Volume up/down via rotary encoder
- Play/pause toggle via center button
- Previous/next track via touch swipe
- Auto-reconnect after reboot
- "Just works" pairing (no PIN required)

### Metadata (AVRCP) - Works with phones
- Track title, artist, album displayed on screen
- Play state and position updates
- Connected device name shown

### Device Compatibility

| Device | Controls | Metadata | Notes |
|--------|----------|----------|-------|
| iPhone | ✓ | ✓ | Full functionality |
| Android | ✓ | ✓ | Full functionality |
| DAPs | ✓ | ✗ | DAPs need A2DP for AVRCP |

## Flashing Instructions

**Important**: This release requires flashing **both chips**.

1. Flash ESP32-S3 firmware (normal orientation)
2. Flip USB-C connector, flash ESP32 firmware from `esp32_bt/`
3. See `docs/usage/DUAL_CHIP_ARCHITECTURE.md` for detailed instructions

## Documentation

New and updated documentation:
- `docs/usage/DUAL_CHIP_ARCHITECTURE.md` - Complete architecture guide
- `docs/esp/BLE_HID.md` - BLE HID implementation details
- `docs/dev/BOOT_SEQUENCE.md` - System initialization
- `docs/dev/FREERTOS_PATTERNS.md` - FreeRTOS usage patterns
- `docs/dev/KCONFIG.md` - Configuration options
- `docs/dev/NVS_STORAGE.md` - Non-volatile storage
- `docs/usage/WIFI_PROVISIONING.md` - WiFi setup flow

## Known Limitations

- Zone picker shows when in BT mode (should show "Exit Bluetooth" instead)
- Metadata not cleared when AVRCP disconnects
- Some DAPs may not maintain AVRCP connection

## What's Changed

### New
- ESP32 Bluetooth firmware (`esp32_bt/`) with BLE HID and AVRCP
- Binary UART protocol for inter-chip communication
- Controller mode abstraction (WiFi vs Bluetooth)
- Bluetooth UI mode with metadata display

### Changed
- Zone picker now includes "Bluetooth" option
- Mode persistence across reboots via NVS

**Full Changelog**: https://github.com/muness/roon-knob/compare/v1.2.12...v1.3.0
