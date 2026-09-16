# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Features
- Now-playing header: demote the zone label to a darker caption grey and fade
  it to a small dim glyph after ~35s in a stable single-zone household,
  reappearing for 5s on zone-name changes or leaving art mode/picker/settings.
  Multi-zone households never see it fade. Applies to every target that draws
  the zone name on its main screen: Dial (`common/ui.c`, LVGL fade + glyph),
  Tough (`tough_app`), Joy (`atom_app`), and Kizz (`m5_beta_app`'s
  `render_semantic_family`/`render_stackchan_delight` layouts; Dial
  Lab/Twist/Remote never draw the zone name and need no change). Slate
  (`rlcd_app`) and Frame (`frame_app`) store a zone name but never render it
  on their main screen, so they are unaffected. See `docs/esp/DISPLAY.md` for
  the presence-policy details.

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
