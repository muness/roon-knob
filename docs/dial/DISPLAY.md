# Display Subsystem

This document covers how HiPhi Dial drives its 360×360 pixel AMOLED display using ESP-IDF and LVGL.

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

### UI task ownership and diagnostics

All LVGL work runs from the dedicated `ui_loop` task, pinned to core 1 with a
16 KiB internal-RAM stack. Wi-Fi and NimBLE execute on core 0. This division is
intentional: the display needs DMA-capable internal RAM and predictable UI
scheduling, while the radio stacks contend for the other core.

The task is created before Wi-Fi starts because Wi-Fi event paths defer UI work
through LVGL. Task creation is checked; a failure logs the available and
largest internal heap blocks instead of continuing with a display that can
never refresh. Boot telemetry records memory around display allocation and UI
initialization, and `ui_loop` logs its stack high-water mark. A static screen
should therefore be investigated as a task/lifecycle or memory problem first,
not treated as an expected provisioning state.

LVGL uses a split heap on Dial: a 24 KiB built-in pool in internal SRAM plus a
72 KiB expansion pool registered from PSRAM immediately after `lv_init()`.
This keeps the total LVGL object budget at 96 KiB while returning 40 KiB of
contiguous internal SRAM to the Bluetooth controller. The display's DMA draw
buffers and the UI task stack remain internal. Boot telemetry reports LVGL
total, free, largest-free, utilization, and fragmentation after UI creation.
See [ESP32-S3 Memory Architecture](MEMORY.md) for initialization order,
allocator policy, cache-safety constraints, and adaptive-UI lifecycle rules.

The display uses two 24-row partial-render buffers (34,560 bytes total) in
DMA-capable internal SRAM. The former 36-row pair consumed 51,840 bytes and,
with Wi-Fi's static buffers and an active BLE controller, produced a measured
`DMA free=19 largest=0` followed by repeated `wifi:m f null` warnings and HTTP
timeouts. The smaller stripes preserve double buffering while reclaiming
17,280 bytes for radio traffic.

#### Contiguous-block rule

`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` is not sufficient evidence that
a task can be created: FreeRTOS needs one contiguous internal allocation for
its stack. On Dial, LVGL's two DMA draw buffers left 108,679 bytes free in
total but only a 31,744-byte largest block. The former 32 KiB UI-stack request
therefore failed, `app_main()` returned, and the initialized panel stayed
static because no `ui_loop` existed.

The shipping 16 KiB UI stack fits this post-LVGL allocation state. Hardware
telemetry with Wi-Fi, the configuration server, and BLE controls active reached
13,052 bytes used, leaving 3,332 bytes. Any change
to LVGL buffers, image decoding, or task stack size must verify both:

1. `largest internal block >= requested task stack` at the creation point; and
2. the UI task's high-water mark during real artwork traffic, not just idle
   boot.

Treat the absence of `UI loop task started on core 1` as a boot failure. It is
not a condition that Wi-Fi provisioning, browser erase, or BLE pairing can
repair.

#### Adaptive UI payloads

Downloaded screen descriptions, parsed component models, inactive-screen
caches, and image decode buffers belong in PSRAM. The HTTP platform API allocates
JSON response bodies explicitly in PSRAM and applies a 256 KiB default ceiling;
adaptive-screen callers can request a smaller schema-specific ceiling with
`platform_http_get_bounded()`. This prevents an oversized or chunked response
from consuming unbounded memory.

The bridge also installs a fixed PSRAM allocator for cJSON and its queued UI
payloads. This matters because the global 16 KiB threshold would otherwise
make those many small allocations prefer internal SRAM. The hooks and their
process-global lifetime are documented in [ESP32-S3 Memory
Architecture](MEMORY.md).

Only the active screen should be materialized as LVGL objects. Fetch, parse,
materialize, then release or evict inactive payloads rather than retaining an
HTTP body, parser tree, decoded assets, and multiple LVGL trees simultaneously.
Display DMA buffers, the LVGL task stack, and latency-sensitive control state
remain in internal RAM. LVGL objects may use either of its registered pools;
measure the active screen with the LVGL heap telemetry rather than assuming all
widget memory remains internal.

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

- Activity detected → reset timers, wake if in art mode/dim/sleep
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
