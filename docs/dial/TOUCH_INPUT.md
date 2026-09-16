# Touch Input

This document covers capacitive touch input using the CST816S controller.

## Hardware Overview

| Component | Model | Interface | Address |
|-----------|-------|-----------|---------|
| Touch controller | CST816 | I2C | 0x15 |
| I2C SDA | GPIO 11 | - | - |
| I2C SCL | GPIO 12 | - | - |

The CST816 is a single-point capacitive touch controller commonly found in smartwatch displays. It reports touch coordinates over I2C when the user touches the screen.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    LVGL Input Processing                     │
│                (button callbacks, gestures)                  │
└───────────────────────────┬─────────────────────────────────┘
                            │ LV_INDEV_STATE_PRESSED/RELEASED
┌───────────────────────────▼─────────────────────────────────┐
│                  lvgl_touch_read_cb()                        │
│              (platform_display_idf.c)                        │
└───────────────────────────┬─────────────────────────────────┘
                            │ tpGetCoordinates()
┌───────────────────────────▼─────────────────────────────────┐
│                    lcd_touch_bsp.c                           │
│              CST816S I2C register reads                      │
└───────────────────────────┬─────────────────────────────────┘
                            │ i2c_read_buff()
┌───────────────────────────▼─────────────────────────────────┐
│                      I2C Master                              │
│                      (i2c_bsp.c)                             │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                  CST816S Touch IC                            │
└─────────────────────────────────────────────────────────────┘
```

## I2C Bus Setup

The I2C bus is shared between the touch controller and haptic motor driver:

```c
i2c_master_bus_config_t i2c_bus_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = GPIO_NUM_12,
    .sda_io_num = GPIO_NUM_11,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
i2c_new_master_bus(&i2c_bus_config, &bus_handle);
```

Devices on this bus:

- **CST816** (0x15) - Touch controller
- **DRV2605** (0x5A) - Haptic motor driver

The bus runs at 300kHz, which is within spec for both devices.

## CST816S Register Map

The touch controller uses a simple register interface. Reading 7 bytes from register 0x00 gives:

| Byte | Content |
|------|---------|
| 0 | Gesture ID (not used) |
| 1 | Touch event (0=down, 1=up, 2=contact) |
| 2 | Number of touch points (0 or 1) |
| 3 | X coordinate high nibble |
| 4 | X coordinate low byte |
| 5 | Y coordinate high nibble |
| 6 | Y coordinate low byte |

Coordinates are 12-bit values (0-4095) but the display is 360×360, so the controller auto-scales to screen resolution.

## Reading Touch Coordinates

```c
uint8_t tpGetCoordinates(uint16_t *x, uint16_t *y) {
    uint8_t data[7] = {0};
    i2c_read_buff(disp_touch_dev_handle, 0x00, data, 7);

    uint8_t touch_count = data[2];
    if (touch_count) {
        *x = ((uint16_t)(data[3] & 0x0f) << 8) + (uint16_t)data[4];
        *y = ((uint16_t)(data[5] & 0x0f) << 8) + (uint16_t)data[6];
        return 1;
    }
    return 0;
}
```

Returns 1 if a touch is active, 0 otherwise. The X/Y coordinates are assembled from 12-bit values split across two bytes.

## LVGL Integration

LVGL polls touch state through an input device callback:

```c
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x, y;

    if (tpGetCoordinates(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        display_activity_detected();  // Wake display if sleeping
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
```

Registration:

```c
s_touch_indev = lv_indev_create();
lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(s_touch_indev, lvgl_touch_read_cb);
```

LVGL calls this callback on every frame (~30Hz). The callback must:

1. Check if a touch is active
2. Report coordinates if touched
3. Report PRESSED or RELEASED state

## Touch Events in LVGL

LVGL handles touch input like a mouse. When you tap a button:

1. `LV_EVENT_PRESSED` - Finger touched the button
2. `LV_EVENT_PRESSING` - Finger still down (repeating)
3. `LV_EVENT_RELEASED` - Finger lifted inside button
4. `LV_EVENT_CLICKED` - Full tap completed (pressed + released)

Or if the user drags off:

1. `LV_EVENT_PRESSED`
2. `LV_EVENT_PRESS_LOST` - Finger dragged outside button bounds

Example button callback:

```c
static void btn_play_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ui_dispatch_input(UI_INPUT_PLAY_PAUSE);
    }
}

// Registration
lv_obj_add_event_cb(s_btn_play, btn_play_event_cb, LV_EVENT_CLICKED, NULL);
```

## Long Press Detection

LVGL has built-in long press support:

```c
static void zone_label_long_press_cb(lv_event_t *e) {
    // User held finger on zone label
    ui_show_settings();
}

lv_obj_add_event_cb(s_zone_label, zone_label_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
```

The long press threshold is configurable in `lv_conf.h` (default ~400ms).

## Display Wake on Touch

Any touch wakes the display from sleep, dim, or art mode:

```c
if (tpGetCoordinates(&x, &y)) {
    display_activity_detected();  // Reset sleep timers, wake if sleeping
    // ... rest of touch handling
}
```

This happens in the touch read callback, so even touches that don't hit any widget still wake the display.

## Initialization Sequence

1. **I2C bus init** - `i2c_master_Init()` creates bus and adds devices
2. **Touch init** - `lcd_touch_init()` writes 0x00 to register 0x00 (wake/reset)
3. **LVGL init** - Application calls `lv_init()`
4. **Input device** - `lv_indev_create()` registers touch callback

Touch input starts working after step 4. Before that, `tpGetCoordinates()` can be called but LVGL won't process the events.

## Common Issues

**Touch not responding**: Check I2C connections. Run `i2c_master_bus_probe()` to verify the device is present at address 0x15.

**Coordinates inverted**: Some displays need X/Y swapped or axis inversion. Modify the coordinate assembly in `tpGetCoordinates()`.

**Touch drift/noise**: The CST816 has internal filtering, but if you see phantom touches, check for electrical noise on I2C lines.

**Touch works but buttons don't respond**: LVGL buttons need proper hit areas. Check that `lv_obj_set_size()` and positions are correct.
