# HiPhi Joy (M5Stack AtomS3 Joystick)

HiPhi Joy (the AtomS3 Joystick target) is a distinct M5 surface, not a small Tough. It
uses the AtomS3's 128×128 display through M5Unified/M5GFX and reads the
joystick base's STM32 coprocessor through the documented I²C interface.

## Hardware contract

- Host: M5Stack AtomS3 (`m5::board_t::board_M5AtomS3`), ESP32-S3.
- Display: 128×128 color LCD, owned by M5GFX.
- Joystick coprocessor: address `0x59`, SDA GPIO38, SCL GPIO39, 400 kHz.
- Registers used by this target: left-stick 8-bit values at `0x10`, right-stick
  12-bit values at `0x20`/`0x22`, and button values at `0x70`. The 12-bit right
  stick reads are normalized at the platform boundary to the UI's 8-bit axis
  contract because the separate right-stick 8-bit summary registers were not
  updating reliably on the qualified firmware revision.
- Four button bits are exposed by `M5Platform` as left, right, A, and B.

The controller UI is glance-first: album art and now-playing text share the
small display, while the sticks and four buttons provide transport, volume,
and zone-picker input. There is no touchscreen assumption.

## Build

```sh
source "$HOME/esp/esp-idf/export.sh"
idf.py -C atom_app set-target esp32s3
idf.py -C atom_app build
```

The target uses the shared NVS/configuration and Wi-Fi provisioning contract.
Do not erase NVS during ordinary firmware updates.
