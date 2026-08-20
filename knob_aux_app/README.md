# Waveshare Knob auxiliary ESP32 parking image

This is a companion image for the **classic ESP32** inside the exact Waveshare
ESP32-S3-Knob-Touch-LCD-1.8. It is not the main HiPhi Dial firmware.

The board schematic ties the auxiliary ESP32 enable pin high and provides no
shutdown line from the primary S3. The vendor factory image contains Classic
Bluetooth, A2DP/AVRCP, encoder, audio, and UART services. HiPhi Dial does not
use that chip, so sleeping only the primary S3 leaves an active radio SoC on
the same battery rail.

This image drives the PCM5100A XSMT input low, holds that level, and immediately
enters ESP32 Deep-sleep with no wake source. A hardware reset or bootloader
entry wakes it only long enough to park again.

## Build

Use the repository's pinned ESP-IDF 5.5.5 environment:

```sh
cd knob_aux_app
idf.py set-target esp32
idf.py build
```

## Flash and recovery

The USB-C orientation selects which SoC is connected to USB. Confirm that the
boot log identifies an **ESP32**, not an ESP32-S3, before flashing. Flashing
the wrong orientation would replace the main controller image.

```sh
idf.py flash monitor
```

The operation is recoverable: the official Waveshare demo archive contains
`ESP32-KNOB_ESP32_0.bin`, which can be reflashed to the same auxiliary chip if
its original Bluetooth/audio behavior is ever needed.

This image reduces one known source of board-level draw; it is not a current
claim. The always-powered 3.3 V converter, display logic, haptic controller,
microphone, SD interface, and other leakage paths still require measurement.
