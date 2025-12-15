#!/bin/bash
# Generate LVGL bitmap fonts from TTF files
# Run from idf_app directory: ./scripts/generate_fonts.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IDF_APP_DIR="$(dirname "$SCRIPT_DIR")"
SPIFFS_DIR="$IDF_APP_DIR/spiffs_data"
OUTPUT_DIR="$IDF_APP_DIR/main/fonts"

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

# Check for lv_font_conv
if ! command -v lv_font_conv &> /dev/null; then
    echo "Error: lv_font_conv not found. Install with: npm install -g lv_font_conv"
    exit 1
fi

echo "Generating bitmap fonts..."

# Unicode ranges for text font:
# - Basic Latin: 0x20-0x7F (ASCII)
# - Latin-1 Supplement: 0xA0-0xFF (Western European accents)
# - Latin Extended-A: 0x100-0x17F (Central European accents)
# - Latin Extended-B: 0x180-0x24F (additional Latin chars)
# - Greek and Coptic: 0x370-0x3FF (Greek alphabet)
# - Cyrillic: 0x400-0x4FF (Russian, Ukrainian, etc.)
# - General Punctuation: 0x2010-0x2027 (dashes, quotes, ellipsis)
# - Arrows: 0x2190-0x2193 (←↑→↓)
TEXT_RANGES="0x20-0x7F,0xA0-0xFF,0x100-0x17F,0x180-0x24F,0x370-0x3FF,0x400-0x4FF,0x2010-0x2027,0x2190-0x2193"

# Material Icons codepoint ranges (using MaterialIcons-Regular.ttf)
# Core icons needed for the UI:
# - Media: play e037, pause e034, skip_next e044, skip_prev e045, music_note e405
# - Volume: volume_up e050, volume_down e04d, volume_mute e04e, volume_off e04f
# - Navigation: arrow_back e5c4, chevron_left e5cb, chevron_right e5cc, settings e8b8
# - Connectivity: bluetooth e1a7, wifi e63e, wifi_off e648, cast e307, speaker e32d
# - Battery: battery_full e1a4, battery_charging e1a3, battery_alert e19c
# - Status: check e5ca, close e5cd, error e000, info e88e, warning e002
# - Misc: download f090, refresh e5d5, home e88a, search e8b6
ICON_RANGES="0xE000,0xE002,0xE034,0xE037,0xE044-0xE045,0xE04D-0xE050,0xE19C,0xE1A3-0xE1A4,0xE1A7,0xE307,0xE32D,0xE405,0xE5C4,0xE5CA-0xE5CD,0xE5D5,0xE63E,0xE648,0xE88A,0xE88E,0xE8B6,0xE8B8,0xF090"

# Generate Charis SIL text fonts at 22, 28, 40px
echo "Converting Charis SIL..."
for size in 22 28 40; do
    echo "  - charis_${size}.c"
    lv_font_conv \
        --bpp 4 \
        --size $size \
        --font "$SPIFFS_DIR/CharisSIL.ttf" \
        --range $TEXT_RANGES \
        --format lvgl \
        --no-compress \
        --no-prefilter \
        -o "$OUTPUT_DIR/charis_${size}.c"
done

# Generate Material Icons icon fonts at 22, 28, 48px
echo "Converting Material Icons..."
for size in 22 28 48; do
    echo "  - material_icons_${size}.c"
    lv_font_conv \
        --bpp 4 \
        --size $size \
        --font "$SPIFFS_DIR/MaterialIcons-Regular.ttf" \
        --range $ICON_RANGES \
        --format lvgl \
        --no-compress \
        --no-prefilter \
        -o "$OUTPUT_DIR/material_icons_${size}.c"
done

# Fix LVGL include path (lv_font_conv generates "lvgl/lvgl.h" but we use "lvgl.h")
echo "Fixing LVGL include paths..."
sed -i '' 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|g' "$OUTPUT_DIR"/*.c

# Set up font fallback: Charis SIL -> Material Symbols (for icons in text)
echo "Setting up font fallback (Charis -> Material Symbols)..."
# Add extern declaration and set fallback for each Charis font size
for size in 22 28 40; do
    icon_size=$size
    # 40px text uses 48px icons (closest match)
    [ "$size" = "40" ] && icon_size=48

    # Add extern declaration after includes
    sed -i '' "s|#include \"lvgl.h\"|#include \"lvgl.h\"\nextern const lv_font_t material_icons_${icon_size};|" "$OUTPUT_DIR/charis_${size}.c"

    # Set fallback pointer
    sed -i '' "s|\.fallback = NULL|.fallback = \&material_icons_${icon_size}|" "$OUTPUT_DIR/charis_${size}.c"
done

echo "Done! Generated fonts in $OUTPUT_DIR"
ls -la "$OUTPUT_DIR"
