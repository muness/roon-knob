# Reference Documentation

Hardware and implementation guides for the Waveshare ESP32-S3 Touch AMOLED 1.8" knob device.

## Hardware Documentation

### Core Hardware
- [Board Overview](hardware/board.md) - Device specs and capabilities
- [Pin Configuration](HARDWARE_PINS.md) - GPIO assignments for all peripherals
- [Display Colors](COLORTEST_HELLOWORLD.md) - SH8601 QSPI color format and byte order

### Peripherals
- [Touch Controller](hardware/cst816d.md) - CST816D integration with LVGL
- [Rotary Encoder](hardware/encoder.md) - Quadrature encoder implementation
- [Battery Monitoring](hardware/battery.md) - ADC-based battery level detection

## Implementation Guides

### Display & Graphics
- [Image Rendering](esp32-s3/image_render.md) - JPEG decoding and artwork display

### UI
- [Now Playing Image](ui/now_playing_image.md) - Artwork fetch and display flow
- [Touch Integration](ui/touch.md) - Touch input handling

## Analysis Documents

Historical analysis of similar projects (kept for reference):
- [Smart Knob Analysis](smart-knob-analysis.md) - Scott Bezek's smart-knob project
- [BlueKnob Analysis](blueknob-analysis.md) - BlueKnob project analysis

## Key Implementation Notes

### Display (SH8601 QSPI AMOLED)
- Resolution: 360×360
- Interface: QSPI (4-wire SPI with quad data)
- **Color format**: RGB565 with byte swap in flush callback
- See [COLORTEST_HELLOWORLD.md](COLORTEST_HELLOWORLD.md) for details

### Input
- **Rotary encoder**: Software quadrature decoding (GPIO 7/8)
- **Touch**: CST816D via I2C (GPIO 11/12), integrated with LVGL
- **No physical buttons** on this device

### Memory
- 16 MB Flash, 8 MB PSRAM
- PSRAM used for LVGL buffers, artwork cache, network buffers
