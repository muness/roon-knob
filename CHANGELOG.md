# Changelog

## 2.8.0-alpha.2

- Stop ordinary Wi-Fi retries from disconnecting a new association before DHCP; cancel stale retries when an IP arrives.

- Preserve stable v2.5.2 settings when its full V2 configuration retains a legacy version marker.

- Reuse a verified bridge address after wake while preserving the selected hostname and zone; fall back to discovery when the address fails.
- Share Wi-Fi readiness ordering across all controllers and clear readiness on IP loss before reconnect work.
- Guard recovery with runtime, NVS persistence and Wi-Fi lifecycle regressions, plus a CI contract keeping every target on the shared path (#284).


All notable changes to this project will be documented in this file.

## [Unreleased]

### Features
- Bridge-served OTA on every controller (implemented, unverified on hardware):
  all targets get factory + `ota_0` + `ota_1` slots, app rollback with a 45 s
  boot confirmation, and the shared OTA client. UHC's Update button requests an
  install through `config.firmware_update`; the device ignores stale requests,
  installs only a strictly newer version, and follows its own prerelease
  channel. Needs a one-time browser reflash of the new partition table that
  declines erase (settings are kept).
- Device-specific settings sync from the bridge config (implemented,
  unverified on hardware): `show_ip` (Frame), `brightness` 5..100 (Dial,
  HALO), `body_enabled`, `sound_enabled`, `sound_volume` 0..2 (Kizz). Missing,
  null, wrong-typed or out-of-range values are ignored; unchanged values are
  not rewritten.
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
