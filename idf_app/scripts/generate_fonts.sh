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
# - Basic Latin: 0x20-0x7F (ASCII) - English alphabet, numbers, punctuation
# - Latin-1 Supplement: 0xA0-0xFF (Western European) - é è ê ë à â ç ö ü ñ
# - Latin Extended-A: 0x100-0x17F (Central European) - ā ă ą ć č ď ě ę ğ
# - Latin Extended-B: 0x180-0x24F (Additional Latin) - ƒ ơ ư ǎ ǐ ǒ ǔ ș ț
# - Greek and Coptic: 0x370-0x3FF (Greek) - α β γ δ ε ζ η θ ι κ λ μ
# - Cyrillic: 0x400-0x4FF (Russian, Ukrainian, etc.) - А Б В Г Д Е Ж З
# - General Punctuation: 0x2010-0x2027 (Typography) - em dash, en dash, quotes, ellipsis
TEXT_RANGES="0x20-0x7F,0xA0-0xFF,0x100-0x17F,0x180-0x24F,0x370-0x3FF,0x400-0x4FF,0x2010-0x2027"

# Material Icons codepoint ranges (using MaterialIcons-Regular.ttf)
# Core icons needed for the UI:
# - Media: play e037, pause e034, skip_next e044, skip_prev e045, music_note e405
# - Volume: volume_up e050, volume_down e04d, volume_mute e04e, volume_off e04f
# - Navigation: arrow_back e5c4, chevron_left e5cb, chevron_right e5cc, settings e8b8
# - Connectivity: bluetooth e1a7, wifi e63e, wifi_off e648, cast e307, speaker e32d
# - Battery: battery_charging e1a3 (Material Icons, kept for fallback)
# - Status: check e5ca, close e5cd, error e000, info e88e, warning e002
# - Misc: download f090, refresh e5d5, home e88a, search e8b6
ICON_RANGES="0xE000,0xE002,0xE034,0xE037,0xE044-0xE045,0xE04D-0xE050,0xE1A3,0xE1A7,0xE307,0xE32D,0xE405,0xE5C4,0xE5CA-0xE5CD,0xE5D5,0xE63E,0xE648,0xE88A,0xE88E,0xE8B6,0xE8B8,0xF090"

# Lucide battery icons (horizontal style) - from lucide.ttf
# battery e053, battery-charging e054, battery-full e055, battery-low e056,
# battery-medium e057, battery-warning e3ac
LUCIDE_BATTERY_RANGES="0xE053-0xE057,0xE3AC"

# Generate Lato text font at 22px (for artist, volume label, status)
echo "Converting Lato..."
echo "  - lato_22.c"
lv_font_conv \
    --bpp 4 \
    --size 22 \
    --font "$SPIFFS_DIR/Lato-Regular.ttf" \
    --range $TEXT_RANGES \
    --format lvgl \
    --no-compress \
    --no-prefilter \
    -o "$OUTPUT_DIR/lato_22.c"

# Generate Noto Sans text font at 28px (for track, zone)
echo "Converting Noto Sans..."
echo "  - notosans_28.c"
lv_font_conv \
    --bpp 4 \
    --size 28 \
    --font "$SPIFFS_DIR/NotoSans-Regular.ttf" \
    --range $TEXT_RANGES \
    --format lvgl \
    --no-compress \
    --no-prefilter \
    -o "$OUTPUT_DIR/notosans_28.c"

# Generate Material Icons icon fonts at 22, 28, 44, 60px
# Sizes 44/60 match enlarged transport buttons (60px/80px backgrounds)
echo "Converting Material Icons..."
for size in 22 28 44 60; do
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

# Generate Lucide battery icons at 22px (horizontal battery indicator)
echo "Converting Lucide battery icons..."
echo "  - lucide_battery_22.c"
lv_font_conv \
    --bpp 4 \
    --size 22 \
    --font "$SPIFFS_DIR/lucide.ttf" \
    --range $LUCIDE_BATTERY_RANGES \
    --format lvgl \
    --no-compress \
    --no-prefilter \
    -o "$OUTPUT_DIR/lucide_battery_22.c"

# Fix LVGL include path (lv_font_conv generates "lvgl/lvgl.h" but we use "lvgl.h")
echo "Fixing LVGL include paths..."
# Cross-platform sed: use .bak extension, then remove backups
sed -i.bak 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|g' "$OUTPUT_DIR"/*.c
rm -f "$OUTPUT_DIR"/*.bak

# Sanitize absolute paths from generated files (removes developer-local paths from Opts: header)
echo "Sanitizing absolute paths..."
for file in "$OUTPUT_DIR"/*.c; do
    # Replace absolute paths with just filenames in the Opts: comment line
    sed -i.bak -E 's|--font [^ ]+/([^/ ]+\.ttf)|--font \1|g; s|-o [^ ]+/([^/ ]+\.c)|-o \1|g' "$file"
done
rm -f "$OUTPUT_DIR"/*.bak

# Set up font fallback: Lato/Noto Sans -> Material Symbols (for icons in text)
echo "Setting up font fallback (Lato/Noto Sans -> Material Symbols)..."

# lato_22 -> material_icons_22
sed -i.bak "s|#include \"lvgl.h\"|#include \"lvgl.h\"\nextern const lv_font_t material_icons_22;|" "$OUTPUT_DIR/lato_22.c"
sed -i.bak "s|\.fallback = NULL|.fallback = \&material_icons_22|" "$OUTPUT_DIR/lato_22.c"
rm -f "$OUTPUT_DIR/lato_22.c.bak"

# notosans_28 -> material_icons_28
sed -i.bak "s|#include \"lvgl.h\"|#include \"lvgl.h\"\nextern const lv_font_t material_icons_28;|" "$OUTPUT_DIR/notosans_28.c"
sed -i.bak "s|\.fallback = NULL|.fallback = \&material_icons_28|" "$OUTPUT_DIR/notosans_28.c"
rm -f "$OUTPUT_DIR/notosans_28.c.bak"

echo "Done! Generated fonts in $OUTPUT_DIR"
ls -la "$OUTPUT_DIR"
