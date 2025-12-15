# Font System

This document describes the font implementation for the Roon Knob display.

## Overview

The display uses two font families:
1. **Charis SIL** - Text font for music metadata (artist, track, album names)
2. **Material Symbols** - Icon font for UI controls (play, pause, settings, etc.)

## Implementation: Pre-rendered Bitmap Fonts

Fonts are converted from TTF to LVGL bitmap format **at build time** using `lv_font_conv`. This approach was chosen after extensive testing of runtime font rendering alternatives.

### Why Not TinyTTF (Runtime Rendering)?

We initially implemented TinyTTF for runtime TTF rendering, which would have provided:
- Full Unicode support from a single TTF file
- Dynamic font scaling
- Smaller flash footprint

However, TinyTTF proved **unstable on ESP32-S3** due to memory constraints:

1. **Glyph rasterization requires significant heap memory** - Each glyph at 36-40px can transiently need 10-30KB during rasterization
2. **LVGL's internal heap is limited** - Must reside in internal SRAM (~170KB max available), not PSRAM
3. **PSRAM not suitable for font heap** - SPI latency causes watchdog timeouts during glyph rendering
4. **Crash symptom**: `assert failed: stbtt__new_active stb_truetype_htcw.h:3160` - memory allocation failure during glyph rasterization

### Bitmap Font Benefits

- **Zero runtime memory pressure** - Glyphs are pre-rendered, no allocation during draw
- **Predictable performance** - No rasterization delays or watchdog risks
- **Reliable operation** - Eliminates crash-prone TinyTTF code path

### Tradeoffs

- **Larger flash usage** - ~1.5MB for all font sizes (vs ~1MB TTF files)
- **Fixed font sizes** - Must choose sizes at build time (22/28/40/48px)
- **Limited Unicode coverage** - Must specify character ranges at build time

## Font Sizes

| Font | Small | Normal | Large |
|------|-------|--------|-------|
| Charis SIL (text) | 22px | 28px | 40px |
| Material Symbols (icons) | 22px | 28px | 48px |

## Unicode Coverage

### Text Font (Charis SIL)

- Basic Latin (U+0020-007F) - ASCII
- Latin-1 Supplement (U+00A0-00FF) - Western European accents
- Latin Extended-A (U+0100-017F) - Central European accents
- Latin Extended-B (U+0180-024F) - Additional Latin characters
- Greek and Coptic (U+0370-03FF) - Greek alphabet
- Cyrillic (U+0400-04FF) - Russian, Ukrainian, Bulgarian, etc.
- General Punctuation (U+2010-2027) - Dashes, quotes, ellipsis

This covers most music metadata including:
- French: é, è, ê, ë, à, â, ç, ô, û, etc.
- German: ä, ö, ü, ß
- Spanish: ñ, á, í, ó, ú
- Portuguese: ã, õ
- Nordic: å, æ, ø
- Greek: α, β, γ, δ, etc.
- Russian/Cyrillic: А, Б, В, Г, etc.
- Em dash, en dash, smart quotes, ellipsis

### Icon Font (Material Symbols)

59 icons for UI controls including:
- Media: play, pause, skip, stop, shuffle, repeat
- Volume: up, down, mute
- Network: WiFi, Bluetooth, signal
- Status: check, close, error, warning
- Navigation: arrows, chevrons, menu, home
- Settings: gear, tune, power, brightness

## Generating Fonts

Run from `idf_app/` directory:

```bash
./scripts/generate_fonts.sh
```

This requires `lv_font_conv`:
```bash
npm install -g lv_font_conv
```

### Adding Characters

To add more Unicode coverage, edit `scripts/generate_fonts.sh`:

```bash
# Add Cyrillic range
TEXT_RANGES="0x20-0x7F,0xA0-0xFF,0x100-0x17F,0x2010-0x2027,0x0400-0x04FF"
```

Then regenerate and rebuild.

### Adding Icons

1. Find the Material Symbols codepoint (e.g., U+E88A for "home")
2. Add to `ICON_RANGES` in `scripts/generate_fonts.sh`
3. Add the UTF-8 encoding to `font_manager.h`:
   ```c
   #define ICON_HOME "\xEE\xA2\x8A"  // U+E88A
   ```
4. Regenerate fonts and rebuild

## Future: Language Packs

For CJK, Arabic, or Cyrillic support, the recommended approach is:

1. Generate separate bitmap font files for each script
2. Load conditionally based on detected metadata language
3. Or offer as build-time configuration options

This maintains the stability benefits while allowing extended language support.

## Files

- `idf_app/scripts/generate_fonts.sh` - Font generation script
- `idf_app/main/fonts/*.c` - Generated bitmap font data
- `idf_app/main/font_manager.c` - Font access API
- `idf_app/main/font_manager.h` - Font API and icon definitions
- `idf_app/spiffs_data/CharisSIL.ttf` - Source TTF (used by generator)
- `idf_app/spiffs_data/MaterialSymbols.ttf` - Source TTF (used by generator)

## Memory Usage

| Component | Size |
|-----------|------|
| charis_22.c | 580 KB |
| charis_28.c | 856 KB |
| charis_40.c | 1620 KB |
| material_icons_22.c | 56 KB |
| material_icons_28.c | 80 KB |
| material_icons_48.c | 200 KB |
| **Total** | **~3.4 MB** |

Note: Text fonts are larger due to Cyrillic and Greek support. The device has
16MB flash, so this is well within capacity.

All font data resides in flash (`.rodata`), not RAM.
