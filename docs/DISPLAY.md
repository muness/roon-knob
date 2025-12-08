# Display Subsystem

This document covers how the Roon Knob drives its 360×360 pixel AMOLED display using ESP-IDF and LVGL.

## Hardware Overview

| Component | Model | Interface | Notes |
|-----------|-------|-----------|-------|
| Display controller | SH8601 | QSPI (4-wire) | IPS LCD, 16-bit RGB565 |
| Resolution | 360×360 | - | Round display |
| Backlight | PWM-controlled | GPIO 47 | 8-bit brightness (0-255) |

The SH8601 is an LCD driver IC that accepts pixel data over Quad SPI, allowing faster transfers than standard SPI by using 4 data lines simultaneously.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Application (ui.c)                      │
│                   LVGL widgets and layouts                   │
└───────────────────────────┬─────────────────────────────────┘
                            │ lv_* API calls
┌───────────────────────────▼─────────────────────────────────┐
│                         LVGL                                 │
│              (managed_components/lvgl__lvgl)                 │
└───────────────────────────┬─────────────────────────────────┘
                            │ flush callback
┌───────────────────────────▼─────────────────────────────────┐
│                 platform_display_idf.c                       │
│           LVGL display driver + ESP-IDF glue                 │
└───────────────────────────┬─────────────────────────────────┘
                            │ esp_lcd_panel_draw_bitmap()
┌───────────────────────────▼─────────────────────────────────┐
│                    ESP-IDF LCD API                           │
│                   (esp_lcd_panel_ops.h)                      │
└───────────────────────────┬─────────────────────────────────┘
                            │ SPI DMA transfer
┌───────────────────────────▼─────────────────────────────────┐
│                     SH8601 Display                           │
└─────────────────────────────────────────────────────────────┘
```

## LVGL Integration

[LVGL](https://lvgl.io/) is a graphics library designed for embedded systems. It provides widgets (buttons, labels, arcs, etc.) and handles rendering to a framebuffer. The firmware uses LVGL 9.x.

### How LVGL Works

LVGL maintains an internal scene graph of widgets. When something changes (text update, animation frame, etc.), LVGL marks affected regions as "dirty". On each frame:

1. LVGL calculates which pixels changed
2. Renders those pixels to a draw buffer
3. Calls your flush callback to push the buffer to hardware

This "partial rendering" approach is memory-efficient - you only need a small buffer, not a full framebuffer.

### Tick Timer

LVGL needs to know how much time has passed for animations and input handling. A periodic timer feeds it time increments:

```c
static void lvgl_tick_timer_cb(void *arg) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);  // Tell LVGL 2ms passed
}

// Created with esp_timer:
esp_timer_start_periodic(s_lvgl_tick_timer, 2 * 1000);  // 2ms in microseconds
```

Without this timer, LVGL animations freeze and input becomes unresponsive.

### Draw Buffers

LVGL renders to RAM buffers that get DMA'd to the display. The firmware uses double-buffering:

```c
size_t buf_size = LCD_H_RES * LVGL_BUF_HEIGHT * sizeof(lv_color_t);
void *buf1 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
void *buf2 = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
lv_display_set_buffers(display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
```

Key points:

- `MALLOC_CAP_DMA` - Buffer must be in DMA-capable memory for SPI transfers
- `MALLOC_CAP_INTERNAL` - Use internal RAM (faster than PSRAM)
- `LVGL_BUF_HEIGHT = 36` - Buffer holds 36 rows (1/10th of display) to save RAM
- Double-buffering lets LVGL render to one buffer while DMA transfers the other

### Flush Callback

The flush callback is where LVGL hands off rendered pixels to hardware:

```c
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // Byte-swap for big-endian display
    uint16_t *pixels = (uint16_t *)px_map;
    for (int i = 0; i < pixel_count; i++) {
        pixels[i] = (pixels[i] >> 8) | (pixels[i] << 8);
    }

    // Send to display hardware
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);

    // Signal LVGL the buffer is free for reuse
    lv_display_flush_ready(disp);
}
```

The byte-swap is necessary because ESP32 is little-endian but the SH8601 expects big-endian RGB565 pixels.

### Rounder Callback

The SH8601 requires 2-pixel alignment for memory writes. LVGL's rounder callback adjusts dirty regions:

```c
static void lvgl_rounder_cb(lv_event_t *e) {
    lv_area_t *area = lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;      // Round down to even
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1; // Round up to odd
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
```

Without this, you'd see visual glitches when rendering to odd pixel boundaries.

## ESP-IDF LCD API

ESP-IDF provides a hardware abstraction for LCD panels. The key functions:

### Panel Configuration

```c
esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = PIN_NUM_LCD_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,  // RGB565
    .vendor_config = &vendor_config,
};
esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle);
```

The `vendor_config` contains SH8601-specific initialization commands - a sequence of register writes that configure the display's internal settings (gamma curves, timing, power, etc.).

### Drawing

```c
esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data);
```

This function:
1. Sets the display's drawing window (which pixels to update)
2. Streams pixel data via SPI DMA
3. Returns immediately while DMA completes in background

### Display Power Control

```c
esp_lcd_panel_disp_on_off(panel_handle, true);   // Turn on
esp_lcd_panel_disp_on_off(panel_handle, false);  // Turn off (sleep)
```

Used by the sleep manager to power down the display during inactivity.

## Backlight Control

The backlight uses PWM (pulse-width modulation) for smooth brightness control:

```c
// Configure PWM channel
ledc_channel_config_t ledc_channel = {
    .gpio_num = PIN_NUM_BK_LIGHT,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .duty = 128,  // 50% brightness (0-255)
};
ledc_channel_config(&ledc_channel);

// Change brightness
ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, new_brightness);
ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
```

The firmware defines brightness levels in Kconfig:

- `CONFIG_RK_BACKLIGHT_NORMAL` - Active brightness
- `CONFIG_RK_BACKLIGHT_DIM` - Dimmed brightness after inactivity

## Display Sleep Management

The display has four states managed by `display_sleep.c`:

| State | Backlight | Panel | Description |
|-------|-----------|-------|-------------|
| `DISPLAY_STATE_NORMAL` | Full | On | Active use |
| `DISPLAY_STATE_ART_MODE` | Full | On | Fullscreen artwork, no controls |
| `DISPLAY_STATE_DIM` | Low | On | Dimmed after inactivity |
| `DISPLAY_STATE_SLEEP` | Off | Off | Deep sleep after extended inactivity |

State transitions are timer-driven:

- Activity detected → reset timers, wake if sleeping
- Dim timeout → transition to DIM
- Sleep timeout → transition to SLEEP

Thread safety is handled via a FreeRTOS mutex - timer callbacks set pending flags that get processed in the main UI loop.

## Pin Mapping

| Signal | GPIO | Notes |
|--------|------|-------|
| SCLK (clock) | 13 | QSPI clock |
| DATA0 | 15 | QSPI data line 0 |
| DATA1 | 16 | QSPI data line 1 |
| DATA2 | 17 | QSPI data line 2 |
| DATA3 | 18 | QSPI data line 3 |
| CS | 14 | Chip select (active low) |
| RST | 21 | Hardware reset (active low) |
| Backlight | 47 | PWM brightness control |

## Initialization Sequence

1. **Backlight PWM** - Configure LEDC timer and channel
2. **SPI bus** - Initialize QSPI with DMA
3. **Panel IO** - Create SPI panel IO handle
4. **Panel driver** - Initialize SH8601 with vendor commands
5. **Panel reset** - Hardware reset via GPIO
6. **I2C bus** - For touch controller (separate from display)
7. **Touch controller** - CST816 initialization

After hardware init:

8. **LVGL init** - `lv_init()` (called by application)
9. **Display driver** - Create LVGL display, allocate buffers
10. **Touch driver** - Register LVGL input device
11. **Tick timer** - Start periodic timer for LVGL

## Common Issues

**Display shows garbage/wrong colors**: Check byte order in flush callback. ESP32 is little-endian, most displays expect big-endian RGB565.

**Animations are jerky/frozen**: Ensure the tick timer is running and calling `lv_tick_inc()` regularly.

**Memory allocation fails**: Draw buffers need DMA-capable memory. Reduce buffer height or check heap usage.

**Display doesn't wake from sleep**: Verify `esp_lcd_panel_disp_on_off(panel, true)` is called before setting backlight.
