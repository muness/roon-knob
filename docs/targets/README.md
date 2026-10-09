# Target Notes

This repository builds nine HiPhi controller profiles and one auxiliary parking image. Select the exact hardware profile before flashing; display shape and an ESP32-S3 label do not establish image compatibility. ESP-IDF 5.5.5 is the current build baseline. The table records configured build geometry; exact-unit validation remains a separate release gate.

| Controller | Exact hardware | Build directory / selector | Stable device slug | Chip / flash / PSRAM mode | Published application |
|---|---|---|---|---|---|
| [HiPhi Dial](#hiphi-dial) | Waveshare ESP32-S3-Knob-Touch-LCD-1.8 | `idf_app` | `hiphi-dial` | ESP32-S3 / 16 MB / octal | `hiphi_dial.bin` |
| [Dial auxiliary image](#dial-auxiliary-parking-image) | Waveshare knob, second ESP32 | `knob_aux_app` | n/a | ESP32 / 4 MB / none | `hiphi_knob_aux_park.bin` |
| [HiPhi Frame](#hiphi-frame) | Waveshare ESP32-S3-PhotoPainter | `frame_app` | `hiphi-frame` | ESP32-S3 / 16 MB / octal | `hiphi_frame.bin` |
| [HiPhi Slate](#hiphi-slate) | Waveshare ESP32-S3-RLCD-4.2 | `rlcd_app` | `hiphi-rlcd` | ESP32-S3 / 16 MB / octal | `hiphi_rlcd.bin` |
| [HiPhi Joy](#hiphi-joy) | M5Stack AtomS3 JoyStick K137 | `atom_app` | `hiphi-joy` | ESP32-S3 / 8 MB / none | `hiphi_joy.bin` |
| [HiPhi HALO](#hiphi-halo) | innoelement HALO TOUCH USB hub dock (V2 board) | `halo_app` | `hiphi-halo` | ESP32-S3 / 16 MB / octal | `hiphi_halo.bin` |
| [HiPhi Tough](#hiphi-tough) | M5Stack Tough K034 | `tough_app` | `hiphi-tough` | ESP32 / 16 MB / quad | `hiphi_tough.bin` |
| [HiPhi Dial Lab](#m5-beta-targets) | M5Stack Dial K130-V11 | `m5_beta_app`, `HIPHI_M5_TARGET=dial` | `hiphi-dial-beta` | ESP32-S3 / 8 MB / none | `hiphi_m5dial.bin` |
| [HiPhi Twist](#m5-beta-targets) | M5StickS3 K150 | `m5_beta_app`, `HIPHI_M5_TARGET=sticks3` | `hiphi-sticks3-beta` | ESP32-S3 / 8 MB / octal | `hiphi_sticks3.bin` |
| [HiPhi Remote](#m5-beta-targets) | M5Stack StopWatch C152 | `m5_beta_app`, `HIPHI_M5_TARGET=stopwatch` | `hiphi-stopwatch-beta` | ESP32-S3 / 16 MB / octal | `hiphi_stopwatch.bin` |
| [Kizz](#m5-beta-targets) | M5StackChan K151 | `m5_beta_app`, `HIPHI_M5_TARGET=stackchan` | `hiphi-kizz-beta` | ESP32-S3 / 16 MB / quad | `hiphi_stackchan.bin` |

The main Waveshare Dial and M5 Dial Lab have different application identities, `hiphi_dial` and `hiphi_m5dial`, as well as different memory profiles. Slate's internal project is `hiphi_rlcd_42`; the published filename remains `hiphi_rlcd.bin`. Device slugs and the `roon_knob*.bin` Dial compatibility aliases remain fixed.

## Capability and support boundaries

All nine controllers compose shared playback state/actions, bridge connectivity/recovery, configuration, Wi-Fi provisioning/scanning, and power diagnostics. Display rendering and physical input remain target-owned. The M5 profiles check the board detected by M5Unified against the compiled profile and fail safely on a mismatch; this does not prove every peripheral works on the owner's revision.

| Capability | Present profiles | Boundary |
|---|---|---|
| BLE HID media remote | Dial, Frame, Slate | Shared optional host with persistent enablement and pairing/status controls. Pairing/reconnect and Wi-Fi coexistence still require exact-artifact hardware tests. M5 builds do not currently enable this component. |
| Browser firmware update preserving settings | All profiles | Target-specific manifests exclude NVS. Tough and the auxiliary image use the ESP32 bootloader offset; controllers use OTA parts, the auxiliary uses a single-app image. Erasing the device or flashing a merged factory image clears settings. |
| Bridge-served on-device OTA | Waveshare Dial | The bridge's existing routes serve the Dial feed. Firmware checks ESP32-S3, 16 MB geometry, and `hiphi_dial` or historical `roon_knob` application identity before erasing an OTA slot. This rejects accidental sibling images; it does not authenticate firmware. Other targets use their exact browser artifacts. |
| Form-native M5 controls | Dial Lab, Twist, Remote, Kizz | Ring interaction, held twist, raise/haptic behavior, and bounded expression are implemented per target. Peripheral and power claims require physical evidence. |
| Local wake phrase and bridge voice | Kizz experimental build only | Compiled out by default because no voice endpoint is available. Source is retained behind `HIPHI_KIZZ_WAKE_WORD=ON`; qualification and household-soak evidence belong to [#248](https://github.com/muness/roon-knob/issues/248). |
| Generic projected application controls | Partial, target-dependent | Shared controller seams and Kizz wire-v0 snapshot/lifecycle groundwork exist. Complete domain-neutral rendering and resource/media negotiation remain tracked in [#195](https://github.com/muness/roon-knob/issues/195), [#236](https://github.com/muness/roon-knob/issues/236), and [#237](https://github.com/muness/roon-knob/issues/237). |

Build success, manifest integrity, and HTTP availability establish reproducible delivery evidence. They do not establish cold boot, display/input behavior, sustained Wi-Fi/BLE operation, battery budgets, or actuator safety. Dial v2.5.2 establishes the hardware-proven SDK baseline; each new artifact and target still needs its own checks. Record exact hardware revision, build SHA, artifact SHA-256, and observed results in the relevant GitHub issue/PR. [#193](https://github.com/muness/roon-knob/issues/193) owns profile verification and [#228](https://github.com/muness/roon-knob/issues/228) owns the cross-target power evidence.

## HiPhi Dial

Fully documented. See [docs/usage/DIAL.md](../usage/DIAL.md) for setup and controls, and [`docs/dial/`](../dial/) for hardware and driver reference:

- [DISPLAY.md](../dial/DISPLAY.md), [TOUCH_INPUT.md](../dial/TOUCH_INPUT.md), [SWIPE_GESTURES.md](../dial/SWIPE_GESTURES.md), [ROTARY_ENCODER.md](../dial/ROTARY_ENCODER.md)
- [BATTERY_MONITORING.md](../dial/BATTERY_MONITORING.md), [FONTS.md](../dial/FONTS.md), [MEMORY.md](../dial/MEMORY.md)
- [hw-reference/](../dial/hw-reference/) — [board.md](../dial/hw-reference/board.md), [HARDWARE_PINS.md](../dial/hw-reference/HARDWARE_PINS.md)

## Dial auxiliary parking image

- [DUAL_CHIP_ARCHITECTURE.md](../dial/DUAL_CHIP_ARCHITECTURE.md) — why the board has a
second ESP32 and what the parking image does with it.

## HiPhi Frame

E-ink controller with BLE HID media-remote behavior.

- [BLE_HID.md](../dial/BLE_HID.md) — shared BLE HID host capability
- [`.oh/ble-hid-host.md`](../../.oh/ble-hid-host.md) — BLE HID host working notes
- [`.oh/frame-recovery.md`](../../.oh/frame-recovery.md) — Frame recovery and salvage record

## HiPhi Slate

Reflective-LCD controller. No dedicated guide yet; the shared controller notes apply.

- [`.oh/controller-boundaries.md`](../../.oh/controller-boundaries.md)
- [`.oh/controller-values.md`](../../.oh/controller-values.md)

## HiPhi Joy

- [hw-reference/board-atom-s3-joystick.md](../dial/hw-reference/board-atom-s3-joystick.md) — AtomS3 + JoyStick hardware contract
- [`.oh/input-bindings.md`](../../.oh/input-bindings.md) — joystick and button binding notes
- [docs/hardware/README.md](../hardware/README.md)

## HiPhi HALO

- `halo_app/` — innoelement HALO TOUCH V2 (ESP32-S3 N16R8), always USB powered; shares the Dial LVGL UI. Set the hub USB3/FLASH switch to FLASH to flash.

## HiPhi Tough

- [M5STACK.md](../dial/M5STACK.md) — build, flash, and controls
- [hw-reference/board-tough.md](../dial/hw-reference/board-tough.md) — hardware contract
- [`.oh/m5-tough.md`](../../.oh/m5-tough.md) — porting record
- [`.oh/power-audit.md`](../../.oh/power-audit.md) — battery and power behavior

## M5 beta targets

HiPhi Dial Lab (K130-V11), HiPhi Twist (K150), HiPhi Remote (C152), and Kizz (K151) all build from `m5_beta_app`.

- [hw-reference/m5-form-native-betas.md](../dial/hw-reference/m5-form-native-betas.md) — form-native beta hardware notes
- [WIFI_SCAN.md](../dial/WIFI_SCAN.md) — Wi-Fi scan behavior and troubleshooting on M5 hardware
- Kizz voice: [dev/KIZZ_VOICE.md](../dev/KIZZ_VOICE.md), [dev/KIZZ_WIRE_V0_BOUNDARY.md](../dev/KIZZ_WIRE_V0_BOUNDARY.md)

## Shared across every target

- [usage/FIRMWARE_FLASHING.md](../usage/FIRMWARE_FLASHING.md)
- [usage/WIFI_PROVISIONING.md](../usage/WIFI_PROVISIONING.md)
- [usage/OTA_UPDATES.md](../usage/OTA_UPDATES.md)
- [dial/FIRMWARE_ARTIFACTS.md](../dial/FIRMWARE_ARTIFACTS.md) — artifact names and aliases
- [dial/NETWORK_IDENTITY.md](../dial/NETWORK_IDENTITY.md) — mDNS, hostname, and wire identity
- [connection-recovery.md](../connection-recovery.md)
- [`.oh/controller-provisioning-config.md`](../../.oh/controller-provisioning-config.md)

Kizz wake detection and voice capture are currently compiled out because no voice endpoint is available. Touch playback, room selection, expressions, motion, and speaker cues remain part of the default build. The [voice implementation notes](../dev/KIZZ_VOICE.md) describe the explicit experimental opt-in.
