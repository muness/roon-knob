# M5Stack targets

HiPhi supports M5Stack hardware as a target family, but the boards are not
interchangeable. The current implementation is for the **M5Stack Tough K034**;
future M5 devices should get an explicit target profile rather than inheriting
Tough assumptions.

## Tough K034 at a glance

- Classic ESP32-D0WDQ6-V3, not ESP32-S3.
- 16 MB flash and 8 MB quad PSRAM.
- 320×240 ILI9342C display with CHSC6540 touch.
- USB-UART bridge commonly appears on macOS as
  `/dev/cu.wchusbserial*`.
- The firmware uses `m5stack/M5Unified` and `m5stack/M5GFX` only in the
  Tough platform component. Dial and RLCD builds explicitly exclude that
  component and do not need either library.

Hardware pins, PMIC sequencing, and touch-controller details live in the
[Tough hardware reference](hw-reference/board-tough.md).

## Build and flash

```bash
source "$HOME/esp/esp-idf/export.sh"
idf.py -C tough_app build
```

For a normal update, write the four target-specific parts and preserve NVS:

```bash
esptool.py --chip esp32 --port /dev/cu.wchusbserialXXXX --baud 460800 \
  write_flash -z \
  0x1000 tough_app/build/bootloader/bootloader.bin \
  0x8000 tough_app/build/partition_table/partition-table.bin \
  0xd000 tough_app/build/ota_data_initial.bin \
  0x10000 tough_app/build/hiphi_tough.bin
```

Do **not** use `erase_flash` for routine testing: it removes Wi-Fi and device
configuration. Do not write the merged image at `0x0` unless a deliberate clean
install is intended. The Tough partition table is factory-only; updates are
USB reflashes rather than OTA.

## First boot and controls

With no stored Wi-Fi credentials, Tough starts its setup AP and displays the AP
name and `192.168.4.1`. Connect to that AP, open the captive portal, choose a
network, and enter its password. A short tap on the album-art thumbnail enters
artwork mode; a tap in artwork mode returns to controls. Long-holding the zone
area opens settings.

The touch path is based on M5Unified's `Touch.getDetail(0)` state machine. Keep
`M5.update()` in the UI loop and process a `wasClicked()`/`touch_end` event only
once; do not re-run control hit-testing after a mode transition.

## Adding another M5 device

Create a new platform/display/input profile with explicit chip, flash, PSRAM,
display, touch, and power capabilities. Keep playback, Wi-Fi, configuration,
and recovery in shared code. Only the target profile should depend on the
M5-specific platform component or add a new M5 library dependency.

