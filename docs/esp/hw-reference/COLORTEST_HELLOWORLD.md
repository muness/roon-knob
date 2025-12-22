# SH8601 QSPI Display Color Configuration (LVGL 9)

This document describes the correct color configuration for the Waveshare ESP32-S3 Touch AMOLED 1.8" display with SH8601 controller using LVGL 9.

## The Problem

Colors appear shifted/wrong on the SH8601 QSPI display. Common symptoms:
- Red appears as a different color
- Green/blue channels seem swapped or mixed
- Test patterns show wrong colors but UI elements render

## Root Cause: Byte Order Mismatch

The SH8601 QSPI display expects **big-endian RGB565** data, but:
- ESP32-S3 is **little-endian**
- LVGL 9 renders in native (little-endian) RGB565

When LVGL generates red (`0xF800`), it's stored in memory as bytes `00 F8`. The SPI sends bytes in order, so the display receives `00 F8` instead of `F8 00`.

## The Solution: Byte Swap in Flush Callback

The correct approach is to swap bytes in the LVGL flush callback before sending to the display.

### Flush Callback Implementation

In `platform_display_idf.c`:

```c
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    // Swap bytes for big-endian QSPI display (SH8601 expects big-endian RGB565)
    // ESP32 is little-endian, so we need to swap each 16-bit pixel
    const int width = offsetx2 - offsetx1 + 1;
    const int height = offsety2 - offsety1 + 1;
    const int pixel_count = width * height;
    uint16_t *pixels = (uint16_t *)px_map;
    for (int i = 0; i < pixel_count; i++) {
        pixels[i] = (pixels[i] >> 8) | (pixels[i] << 8);
    }

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    lv_display_flush_ready(disp);
}
```

### JPEG Decoder Configuration

Use little-endian output from the JPEG decoder - the flush callback will swap it:

```c
jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;  // Little-endian (flush callback swaps)
cfg.rotate = JPEG_ROTATE_0D;

// Image descriptor uses standard RGB565
out_img->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
```

### Raw Pixel Data

When generating raw RGB565 pixel data (e.g., test patterns), use standard RGB565 values - the flush callback handles byte swapping:

```c
uint16_t color = lv_color_to_u16(lv_color_make(0xFF, 0x00, 0x00));  // Red
// No manual swap needed - flush callback handles it

img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;  // Standard format
```

## Panel Configuration

Standard SH8601 configuration:

```c
const esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = PIN_NUM_LCD_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,  // RGB565
    .vendor_config = &vendor_config,
};
```

## What Does NOT Work

### ❌ `LV_COLOR_FORMAT_RGB565_SWAPPED` on Display

```c
// DO NOT DO THIS - breaks LVGL rendering entirely
lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565_SWAPPED);
```

`LV_COLOR_FORMAT_RGB565_SWAPPED` is only valid for image data descriptors, not as a display output format. Setting this on the display causes LVGL to produce blank/black output.

## Test Pattern

The `ui_test_pattern()` function in `ui.c` can be used for debugging. It displays:
- Top quarter: RED
- Second quarter: GREEN
- Third quarter: BLUE
- Bottom quarter: WHITE

Call it from `ui_init()` to verify colors are correct, then remove the call.

## Summary

| Component | Format | Notes |
|-----------|--------|-------|
| Display driver | Standard RGB565 | No special color format setting |
| Flush callback | Byte swap | Swaps each 16-bit pixel before send |
| JPEG decoder | `JPEG_PIXEL_FORMAT_RGB565_LE` | Little-endian, flush swaps it |
| Image descriptors | `LV_COLOR_FORMAT_RGB565` | Standard format |
| Raw pixel data | Standard RGB565 values | No manual swap needed |

## Implementation Files

- `idf_app/main/platform_display_idf.c` - Flush callback with byte swap
- `common/ui_jpeg.c` - JPEG decoder configuration
- `common/ui.c` - Test pattern for debugging
