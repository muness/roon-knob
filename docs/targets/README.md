# Target Notes

An index of what documentation exists per HiPhi target. The Dial is the most
thoroughly documented target; the rest are covered by a mix of docs and working
notes under `.oh/`. Working notes are session records, not polished guides — read
them as provenance.

| Controller | Hardware | Target dir | Slug |
|---|---|---|---|
| [HiPhi Dial](#hiphi-dial) | Waveshare ESP32-S3-Knob-Touch-LCD-1.8 | `idf_app` | `hiphi-dial` |
| [Dial auxiliary image](#dial-auxiliary-parking-image) | Waveshare knob, second ESP32 | `knob_aux_app` | n/a |
| [HiPhi Frame](#hiphi-frame) | Waveshare ESP32-S3-PhotoPainter | `frame_app` | `hiphi-frame` |
| [HiPhi Slate](#hiphi-slate) | Waveshare ESP32-S3-RLCD-4.2 | `rlcd_app` | `hiphi-rlcd` |
| [HiPhi Joy](#hiphi-joy) | M5Stack AtomS3 JoyStick K137 | `atom_app` | `hiphi-joy` |
| [HiPhi Tough](#hiphi-tough) | M5Stack Tough K034 | `tough_app` | `hiphi-tough` |
| [M5 beta targets](#m5-beta-targets) | Dial Lab, Twist, Remote, Kizz | `m5_beta_app` | `hiphi-*-beta` |

## HiPhi Dial

Fully documented. See [docs/usage/DIAL.md](../usage/DIAL.md) for setup and
controls, and [`docs/dial/`](../dial/) for hardware and driver reference:

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

Reflective-LCD controller. No dedicated guide yet; the shared controller notes
apply.

- [`.oh/controller-boundaries.md`](../../.oh/controller-boundaries.md)
- [`.oh/controller-values.md`](../../.oh/controller-values.md)

## HiPhi Joy

- [hw-reference/board-atom-s3-joystick.md](../dial/hw-reference/board-atom-s3-joystick.md) — AtomS3 + JoyStick hardware contract
- [`.oh/input-bindings.md`](../../.oh/input-bindings.md) — joystick and button binding notes
- [docs/hardware/README.md](../hardware/README.md)

## HiPhi Tough

- [M5STACK.md](../dial/M5STACK.md) — build, flash, and controls
- [hw-reference/board-tough.md](../dial/hw-reference/board-tough.md) — hardware contract
- [`.oh/m5-tough.md`](../../.oh/m5-tough.md) — porting record
- [`.oh/power-audit.md`](../../.oh/power-audit.md) — battery and power behavior

## M5 beta targets

HiPhi Dial Lab (K130-V11), HiPhi Twist (K150), HiPhi Remote (C152), and Kizz (K151)
all build from `m5_beta_app`.

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
