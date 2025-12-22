# Waveshare ESP32-S3 Touch AMOLED 1.8" – Board Overview

## Summary

- **MCU:** ESP32-S3 dual-core @ 240 MHz
- **Wireless:** 2.4 GHz Wi-Fi (802.11 b/g/n) + Bluetooth 5 (LE)
- **Memory:** 16 MB Flash + 8 MB PSRAM (octal)
- **Display:** 1.8" round AMOLED, 360×360, 16.7M colors
- **Display Controller:** SH8601 via QSPI
- **Touch:** CST816D capacitive touch via I2C
- **Input:** Rotary encoder (quadrature, no push button)
- **Power:** LiPo battery support with charging

## Key Specs

| Feature | Specification |
|---------|---------------|
| MCU | ESP32-S3-WROOM-1 (dual-core Xtensa LX7) |
| Clock | Up to 240 MHz |
| Flash | 16 MB (quad SPI) |
| PSRAM | 8 MB (octal SPI) |
| SRAM | 512 KB internal |
| Display | 360×360 AMOLED, SH8601 controller |
| Display Interface | QSPI (4-wire SPI with quad data) |
| Touch | CST816D, I2C @ 0x15 |
| Encoder | EC11-style quadrature (rotation only) |
| USB | USB-C (native USB for programming/serial) |

## What Matters for Firmware

### Display
- 360×360 SH8601 AMOLED over QSPI
- Requires byte-swapped RGB565 (big-endian) - handled in flush callback
- No PWM backlight control needed (AMOLED is self-emitting)

### Touch
- CST816D on I2C (GPIO 11/12)
- 12-bit coordinate resolution
- Integrated with LVGL as pointer input device

### Input
- Rotary encoder on GPIO 7/8 for volume control
- **No physical buttons** - use touch for play/pause, menus

### Memory
- 8 MB PSRAM allows generous LVGL buffers
- DMA-capable internal RAM for display transfers
- PSRAM for artwork caching, network buffers

## Related Docs

- Pin assignments: [HARDWARE_PINS.md](HARDWARE_PINS.md)
- Touch controller: [cst816d.md](cst816d.md)
- Rotary encoder: [encoder.md](encoder.md)
- Display colors: [COLORTEST_HELLOWORLD.md](COLORTEST_HELLOWORLD.md)
