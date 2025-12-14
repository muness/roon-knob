# Changelog

All notable changes to this project will be documented in this file.

## [v1.3.4] - 2024-12-14

### Bug Fixes
- Fix WiFi credentials wiped when bridge URL empty (#11)

## [v1.3.3] - 2024-12-14

### Bug Fixes
- Fix WiFi credential persistence + improve setup UX
- Add save verification and countdown display

## [v1.3.2] - 2024-12-14

### Bug Fixes
- CI: Fix release artifact paths

## [v1.3.1] - 2024-12-14

### Bug Fixes
- Bridge: Fix pairing persistence
- CI: Reduce duplicate builds

## [v1.3.0] - 2024-12-13

### Features
- Bluetooth mode via ESP32 UART (dual-chip architecture)
- BLE HID + AVRCP controller support
- Exit Bluetooth confirmation dialog

### Bug Fixes
- Zone selector overlay hides before mode change
- WiFi provisioning reboots after saving credentials
- On-demand BT activation

## [v1.2.12] - 2024-12-10

### Features
- WiFi provisioning via captive portal (SoftAP)
- mDNS bridge discovery
- Album artwork display

### Bug Fixes
- Volume overlay visibility
- Font rendering improvements
