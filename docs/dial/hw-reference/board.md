# Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — HiPhi Dial board overview

This is the hardware identity record for the Waveshare target in `idf_app`. Vendor specifications describe the product; repository observations describe the firmware. Neither establishes the fitted parts or board revision of a particular owned unit. These bounded corrections salvage the useful identity evidence from [#211](https://github.com/muness/roon-knob/issues/211) and [PR #212](https://github.com/muness/roon-knob/pull/212).

## Vendor specifications

The [Waveshare product specification](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) names this product **ESP32-S3-Knob-Touch-LCD-1.8**. Its own LCD specifications and onboard-resources list are the source for the following table; the comparison tables for other products on that page describe different boards. The [vendor wiki](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) provides setup and demo sources.

| Feature | Vendor-declared specification |
|---------|-------------------------------|
| Primary MCU | ESP32-S3R8, dual-core Xtensa LX7, up to 240 MHz |
| Secondary MCU | ESP32-U4WDH, with 4 MB in-package flash |
| Main memory | 16 MB external flash, 8 MB PSRAM, 512 KB SRAM |
| Panel | 1.8-inch round IPS LCD, 360 × 360, 262K colours |
| Panel driver IC | ST77916 |
| Panel interface | QSPI |
| Touch | Capacitive CST816 family over I²C |
| Encoders | Two, serving the ESP32-S3 and auxiliary ESP32 respectively |
| Other controls | Power and ESP32-S3 BOOT buttons |
| USB | USB-C routing shared between the two MCUs |

The vendor names the touch family CST816. The precise fitted suffix remains a physical-inventory question. A firmware component or source comment is insufficient evidence to identify the panel silicon or fitted touch variant.

## Repository observations

| Firmware behaviour | Source |
|--------------------|--------|
| Uses the `esp_lcd_sh8601` software component with a project-supplied panel initialization array and 16-bit pixel configuration | [Display platform](../../../idf_app/main/platform_display_idf.c), [component manifest](../../../idf_app/main/idf_component.yml) |
| QSPI pins are CS 14, clock 13, data 15/16/17/18, reset 21 | [Display platform](../../../idf_app/main/platform_display_idf.c) |
| Drives a PWM backlight on GPIO 47 with 8-bit LEDC duty at 5 kHz | [Display platform](../../../idf_app/main/platform_display_idf.c) |
| Initial backlight duty comes from `CONFIG_RK_BACKLIGHT_NORMAL`; its default is 100 of 255, about 39% | [Display platform](../../../idf_app/main/platform_display_idf.c), [Kconfig](../../../idf_app/main/Kconfig.projbuild) |
| Reads one quadrature encoder on GPIO 8 and GPIO 7; no encoder shaft switch is read | [Input platform](../../../idf_app/main/platform_input_idf.c) |
| Reads touch over I²C at `0x15`, with SDA 11 and SCL 12 | [Touch BSP](../../../idf_app/components/lcd_touch_bsp/lcd_touch_bsp.c), [I²C BSP](../../../idf_app/components/i2c_bsp/i2c_bsp.c) |
| Byte-swaps RGB565 pixels for panel transport | [Display platform](../../../idf_app/main/platform_display_idf.c) |

`esp_lcd_sh8601` names the software component used to transmit the initialization sequence and pixels. Waveshare declares ST77916 as the panel driver IC. Those are separate facts. The stale source comment about 50% initial backlight does not override the Kconfig value or the channel's actual duty assignment.

## Physical inventory still required

Record the exact owned board revision and fitted part markings before claiming they match the vendor specification. Encoder shaft-switch presence, touch suffix, and measured power or backlight behaviour require physical evidence. Flash geometry from a build configuration does not prove the capacity of an owned unit. Exact firmware flash, sustained boot, display, touch, encoder, and connectivity checks remain the release qualification boundary; this documentation correction adds no new hardware qualification.

## Memory allocation in the firmware

- PSRAM holds artwork, bounded network/JSON payloads, inactive UI models, and part of LVGL's object heap.
- QSPI DMA draw buffers and cache-sensitive task stacks remain in internal SRAM; free PSRAM cannot satisfy those allocations.
- Dial uses a 24 KiB internal + 72 KiB PSRAM split LVGL object heap. See [ESP32-S3 Memory Architecture](../MEMORY.md) for the allocation policy and measured failure history.

## Related documentation

- [Pin assignments](HARDWARE_PINS.md)
- [Touch driver notes](cst816d.md)
- [Rotary encoder](encoder.md)
- [Display colours](COLORTEST_HELLOWORLD.md)
- [Memory allocation policy](../MEMORY.md)
