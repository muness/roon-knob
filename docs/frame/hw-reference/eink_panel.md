# 7.3" E-Ink Panel — Spectra 6 (E6) ACeP

Hardware reference for the Waveshare ESP32-S3-PhotoPainter's e-ink display panel.

## Panel Specs

| Parameter | Value |
|-----------|-------|
| Size | 7.3 inch diagonal |
| Resolution | 800 x 480 pixels |
| PPI | 127 |
| Technology | E Ink Spectra 6 (E6) — Advanced Color ePaper |
| Colors | 6: Black, White, Yellow, Red, Blue, Green |
| Bits per pixel | 4 (2 pixels per byte in framebuffer) |
| Framebuffer size | 192,000 bytes (800 x 480 / 2) |
| Interface | 4-wire SPI (mode 0, CPOL=0, CPHL=0) |
| Max SPI clock | 20 MHz (spec); we run 40 MHz (works in practice) |
| Full refresh time | 12–30 seconds (temperature-dependent) |
| Recommended min refresh interval | 180 seconds (per Waveshare) |
| Operating temperature | 0–50 °C |
| Storage temperature | -25–60 °C |
| Supply voltage (VDD) | 2.5–3.6V (typ 3.0V) |
| Deep sleep current | Single-digit µA |
| Waveform storage | On-chip OTP (not upgradable) |

## Pin Assignments (PhotoPainter Board)

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| MOSI (SDA) | 11 | Output | SPI data |
| SCLK (SCL) | 10 | Output | SPI clock |
| DC (D/C#) | 8 | Output | 0=command, 1=data |
| CS (CS#) | 9 | Output | Active low, manual control |
| RST (RES#) | 12 | Output | Active low reset |
| BUSY (BUSY_N) | 13 | Input | LOW=busy, HIGH=ready |

## Color Index Map

4-bit pixel values sent to the panel controller:

| Color | Index | Hex | RGB888 (for dithering) |
|-------|-------|-----|------------------------|
| Black | 0 | 0x0 | (0, 0, 0) |
| White | 1 | 0x1 | (255, 255, 255) |
| Yellow | 2 | 0x2 | (255, 255, 0) |
| Red | 3 | 0x3 | (255, 0, 0) |
| *(unused)* | 4 | 0x4 | — |
| Blue | 5 | 0x5 | (0, 0, 255) |
| Green | 6 | 0x6 | (0, 255, 0) |

Index 4 is skipped — do not use. The original PhotoPainter firmware's `ColorSelection` enum confirms this gap.

## Command Set

The panel controller is minimal. Only 5 commands are documented:

| Command | Hex | Description | Data bytes |
|---------|-----|-------------|------------|
| Power OFF | 0x02 | Turn off internal DC-DC | 1 byte (0x00) |
| Power ON | 0x04 | Turn on internal DC-DC, wait BUSY | None |
| Deep Sleep | 0x07 | Enter deep sleep (~µA). **Exit requires hardware reset (RST pin toggle).** | 1 byte (0xA5 = enter) |
| Data Start | 0x10 | Begin pixel data transfer to display RAM | 192,000 bytes follow |
| Display Refresh | 0x12 | Apply waveform, update panel. BUSY goes LOW during refresh. | 1 byte (0x00 or 0x01) |

### Init Sequence (from PhotoPainter firmware)

```
0xAA → 0x49, 0x55, 0x20, 0x08, 0x09, 0x18   // Panel config
0x01 → 0x3F                                    // Gate setting
0x00 → 0x5F, 0x69                              // Source/gate driver
0x03 → 0x00, 0x54, 0x00, 0x44                  // Frame memory
0x05 → 0x40, 0x1F, 0x1F, 0x2C                  // Frame control
0x06 → 0x6F, 0x1F, 0x17, 0x49                  // Booster soft start
0x08 → 0x6F, 0x1F, 0x1F, 0x22                  // Update sequence
0x30 → 0x03                                    // PLL/oscillator
0x50 → 0x3F                                    // VCOM interval
0x60 → 0x02, 0x00                              // Data entry mode
0x61 → 0x03, 0x20, 0x01, 0xE0                  // Resolution (800x480)
0x84 → 0x01                                    // Booster control
0xE3 → 0x2F                                    // Power saving
0x04 → (wait BUSY)                             // Power ON
```

### Refresh Sequence

```
0x10 → [192,000 bytes pixel data]    // Load framebuffer
0x04 → (wait BUSY)                   // Power ON
0x06 → 0x6F, 0x1F, 0x17, 0x49       // Booster params
0x12 → 0x00 (wait BUSY ~15-25s)     // Display refresh
0x02 → 0x00 (wait BUSY)             // Power OFF
```

### Deep Sleep Sequence

```
0x07 → 0xA5                         // Enter deep sleep
// To wake: toggle RST pin (LOW 20ms, HIGH 50ms)
```

## What This Panel Cannot Do

- **No partial refresh.** Full-screen only. Waveshare docs: "currently only some black and white e-Paper screens support partial refreshing."
- **No windowed/region updates.** No command exists to define an update region.
- **No hardware dithering.** All dithering must be done host-side before sending pixel data.
- **No fast refresh mode.** No grayscale-only or B&W-only fast mode.
- **No E Ink Ripple.** This is a 2025 waveform upgrade requiring newer OTP and controller chips (T2000). Our panel's waveforms are factory-burned and cannot be updated.

## Dithering

The panel displays 6 discrete colors. To render photographic images, we dither on the ESP32 before sending pixel data.

### Floyd-Steinberg (current implementation)

Error-diffusion dithering across the full 6-color palette. Good for photographic content with smooth gradients. Computationally moderate — processes each pixel once with error propagation to 4 neighbors.

### Checkerboard / Ordered Dithering (alternative)

At 127 PPI, individual pixels are visible. A 50% checkerboard between two colors creates convincing intermediate colors:

| Checkerboard Mix | Perceived Color |
|-----------------|----------------|
| Red + Yellow | Orange |
| Red + Blue | Purple |
| Blue + Green | Cyan |
| Red + White | Light Pink |
| Red + Black | Brown |
| Blue + White | Light Blue |
| Green + White | Light Green |

Computationally cheaper (no error propagation), potentially cleaner visual result at this PPI. See [myembeddedstuff.com research](https://myembeddedstuff.com/e-ink-spectra-6-color).

### Dither Pipeline

```
RGB565 (from bridge) → RGB888 → Floyd-Steinberg → palette index → panel color index
```

The palette array index (0–5) differs from the panel color index (0,1,2,3,5,6) due to the gap at index 4. `eink_palette_to_panel()` handles this mapping.

## Power Architecture

The e-ink panel is powered by the AXP2101 PMIC via ALDO outputs:

| Rail | Voltage | Register | Purpose |
|------|---------|----------|---------|
| DC1 | 3.3V | 0x82 = 0x12 | Main logic |
| ALDO1 | 3.3V | 0x92 = 0x1C | Panel power |
| ALDO2 | 3.3V | 0x93 = 0x1C | Panel power |
| ALDO3 | 3.3V | 0x94 = 0x1C | Panel power |
| ALDO4 | 3.3V | 0x95 = 0x1C | Panel power |

**Critical:** PMIC must be initialized and ALDOs enabled BEFORE any SPI commands to the panel. ALDO enable is bits 0–3 of register 0x90.

## BUSY Pin Behavior

- **HIGH (1)** = panel idle, ready for commands
- **LOW (0)** = panel busy (refreshing, powering on/off)
- During `Display Refresh` (0x12), BUSY goes LOW for 12–30 seconds
- Timeout: 30 seconds is safe; anything longer indicates a problem

## 180° Rotation

The panel's scan direction means the image appears upside-down relative to the PhotoPainter frame's stand orientation. We rotate the framebuffer 180° before sending:

1. Reverse row order (swap top ↔ bottom rows)
2. Reverse byte order within each row
3. Swap nibbles within each byte (pixel order)

This is done on a PSRAM temp buffer to keep the working framebuffer in normal orientation.

## Key Source Files

| File | Purpose |
|------|---------|
| `frame_app/main/eink_display.c` | SPI driver, panel init, refresh, rotation |
| `frame_app/main/eink_display.h` | Pin defs, color enum, dimensions |
| `frame_app/main/eink_dither.c` | Floyd-Steinberg dithering, color mapping |
| `frame_app/main/eink_ui.c` | UI renderer, artwork download, text layout |
| `frame_app/main/pmic_axp2101.c` | PMIC I2C driver, ALDO power rail setup |

## References

- [Waveshare 7.3inch e-Paper HAT (E) Manual](https://www.waveshare.com/wiki/7.3inch_e-Paper_HAT_(E)_Manual)
- [Waveshare E-Paper API Analysis](https://www.waveshare.com/wiki/E-Paper_API_Analysis)
- [E Ink Spectra 6 product page](https://www.eink.com/brand/detail/Spectra6)
- [Dithering on Spectra 6 displays](https://myembeddedstuff.com/e-ink-spectra-6-color)
- 7.3inch e-Paper (E) User Manual PDF (panel datasheet)
