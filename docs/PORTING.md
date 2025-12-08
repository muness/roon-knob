# Porting Guide

This guide covers two scenarios:
1. **Building a different app** for the Waveshare ESP32-S3 Knob hardware
2. **Porting Roon Knob** to a different ESP32-S3 display board

## Architecture Overview

The codebase uses a platform abstraction layer:

```
┌─────────────────────────────────────────────────────────────────┐
│                     Application Layer                           │
│                                                                 │
│   common/ui.c          - LVGL UI (platform-independent)         │
│   common/roon_client.c - Roon API client                        │
│   common/app_main.c    - Application entry point                │
└────────────────────────────────┬────────────────────────────────┘
                                 │
┌────────────────────────────────┴────────────────────────────────┐
│                   Platform Abstraction (common/platform/)       │
│                                                                 │
│   platform_display.h   - Display queries                        │
│   platform_input.h     - Input event handling                   │
│   platform_storage.h   - Configuration persistence              │
│   platform_http.h      - HTTP client                            │
│   platform_mdns.h      - Service discovery                      │
│   platform_time.h      - Timing functions                       │
│   platform_log.h       - Logging                                │
└────────────────────────────────┬────────────────────────────────┘
                                 │
          ┌──────────────────────┴──────────────────────┐
          │                                             │
          ▼                                             ▼
┌─────────────────────────┐               ┌─────────────────────────┐
│   ESP-IDF Implementation │               │   PC Simulator          │
│   (idf_app/main/)        │               │   (pc_sim/)             │
│                          │               │                         │
│   platform_display_idf.c │               │   SDL2 + LVGL           │
│   platform_input_idf.c   │               │   platform_input_pc.c   │
│   platform_storage_idf.c │               │   platform_storage_pc.c │
│   platform_http_idf.c    │               │   platform_http_pc.c    │
│   ...                    │               │   ...                   │
└─────────────────────────┘               └─────────────────────────┘
```

## Scenario 1: Different App, Same Hardware

If you want to build a different application for the Waveshare ESP32-S3 Knob (e.g., a smart home controller, music visualizer, etc.):

### What You Can Reuse

| Component | Files | Notes |
|-----------|-------|-------|
| Display init | `platform_display_idf.c` | SH8601 QSPI setup, LVGL driver |
| Touch input | `lcd_touch_bsp.c`, touch code in display | CST816 I2C |
| Rotary encoder | `platform_input_idf.c` | Quadrature decoding |
| Battery monitoring | `battery.c` | ADC, voltage curve |
| WiFi provisioning | `wifi_manager.c`, `captive_portal.c` | STA/AP modes |
| Display sleep | `display_sleep.c` | Dim/sleep timers |
| I2C bus | `i2c_bsp.c` | Shared by touch + haptic |

### What You Replace

| Component | Replace With |
|-----------|--------------|
| `common/ui.c` | Your LVGL UI code |
| `common/roon_client.c` | Your application logic |
| `common/app_main.c` | Your entry point |
| `rk_cfg.h` | Your configuration struct |

### Minimal Starting Point

```c
// your_app/main.c
#include "platform_display_idf.h"
#include "platform/platform_input.h"
#include "battery.h"
#include "lvgl.h"

void app_main(void) {
    // Initialize NVS
    nvs_flash_init();

    // Initialize display hardware
    platform_display_init();

    // Initialize LVGL
    lv_init();
    platform_display_register_lvgl_driver();

    // Initialize your UI
    my_ui_init();

    // Initialize input
    platform_input_init();

    // Main loop
    while (true) {
        platform_input_process_events();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Hardware Constants Reference

All pin definitions for this hardware:

```c
// Display (platform_display_idf.c)
#define PIN_NUM_LCD_CS      GPIO_NUM_14
#define PIN_NUM_LCD_PCLK    GPIO_NUM_13
#define PIN_NUM_LCD_DATA0   GPIO_NUM_15
#define PIN_NUM_LCD_DATA1   GPIO_NUM_16
#define PIN_NUM_LCD_DATA2   GPIO_NUM_17
#define PIN_NUM_LCD_DATA3   GPIO_NUM_18
#define PIN_NUM_LCD_RST     GPIO_NUM_21
#define PIN_NUM_BK_LIGHT    GPIO_NUM_47

// I2C Bus (settings.h)
#define ESP32_SCL_NUM       GPIO_NUM_12
#define ESP32_SDA_NUM       GPIO_NUM_11

// I2C Devices (settings.h)
#define TOUCH_ADDR          0x15    // CST816
#define DRV2605_ADDR        0x5A    // Haptic motor

// Rotary Encoder (platform_input_idf.c)
#define ENCODER_GPIO_A      GPIO_NUM_8
#define ENCODER_GPIO_B      GPIO_NUM_7

// Battery ADC (battery.c)
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0  // GPIO1
```

---

## Scenario 2: Roon Knob on Different Hardware

To port Roon Knob to a different ESP32-S3 display board:

### Files to Modify

#### 1. Display Driver (`idf_app/main/platform_display_idf.c`)

**Pin definitions** (lines 43-50):
```c
// Change these for your hardware
#define PIN_NUM_LCD_CS      ((gpio_num_t)YOUR_CS_PIN)
#define PIN_NUM_LCD_PCLK    ((gpio_num_t)YOUR_CLK_PIN)
#define PIN_NUM_LCD_DATA0   ((gpio_num_t)YOUR_D0_PIN)
// ... etc
```

**Display controller**: If not using SH8601:
1. Find or write an ESP-IDF component for your display IC
2. Replace `esp_lcd_new_panel_sh8601()` with your driver
3. Adjust initialization commands

**Resolution**: Search for `360` and update:
- `LCD_H_RES`, `LCD_V_RES`
- LVGL buffer calculations
- Touch coordinate scaling (if needed)

**Interface type**: The code uses QSPI. For SPI or parallel RGB:
- Rewrite the SPI bus initialization
- Use appropriate `esp_lcd_panel_io_*` functions

#### 2. Touch Controller (`idf_app/components/lcd_touch_bsp/`)

If not using CST816:
1. Change I2C address in `settings.h`
2. Rewrite `tpGetCoordinates()` for your touch IC's register layout
3. Adjust touch callback in `platform_display_idf.c`

#### 3. I2C Pins (`idf_app/components/i2c_bsp/settings.h`)

```c
#define ESP32_SCL_NUM (GPIO_NUM_YOUR_SCL)
#define ESP32_SDA_NUM (GPIO_NUM_YOUR_SDA)
```

#### 4. Rotary Encoder (`idf_app/main/platform_input_idf.c`)

If your board has different encoder pins:
```c
#define ENCODER_GPIO_A    GPIO_NUM_YOUR_A
#define ENCODER_GPIO_B    GPIO_NUM_YOUR_B
```

If no encoder, stub out `platform_input_init()` or implement alternative input.

#### 5. Battery Monitoring (`idf_app/main/battery.c`)

If different ADC channel or voltage divider:
```c
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_X
#define BATTERY_VOLTAGE_DIVIDER 2.0f  // Adjust for your circuit
```

If no battery, return dummy values or disable.

#### 6. Backlight (`idf_app/main/display_sleep.c`)

```c
#define PIN_NUM_BK_LIGHT    ((gpio_num_t)YOUR_BACKLIGHT_PIN)
```

If PWM backlight not available, adjust `display_set_backlight()`.

#### 7. Kconfig/sdkconfig

Update `sdkconfig.defaults` for your hardware:
- Flash size
- PSRAM configuration (if different)
- Partition table

### Example: Porting to LilyGO T-Display-S3

Hypothetical changes for a common alternative board:

| Component | Waveshare Knob | T-Display-S3 |
|-----------|----------------|--------------|
| Display | SH8601 360×360 QSPI | ST7789 170×320 SPI |
| Touch | CST816 I2C | None (buttons only) |
| Encoder | GPIO 7/8 | None |
| Input | Encoder + touch | Two buttons |

**Changes needed:**
1. Replace SH8601 driver with ST7789
2. Change from QSPI to standard SPI
3. Update resolution to 170×320
4. Remove touch code, add button input
5. Adjust UI layout for different aspect ratio

### Testing Your Port

1. **Display test**: Show a solid color, then a test pattern
2. **Touch test**: Log coordinates on touch
3. **Input test**: Log encoder/button events
4. **WiFi test**: Connect to network, verify IP
5. **Full test**: Connect to Roon bridge

---

## Creating a New Platform

To add support for an entirely new platform (not ESP-IDF):

### 1. Implement Platform Headers

Create implementations for each header in `common/platform/`:

```c
// your_platform/platform_storage_xxx.c
#include "platform/platform_storage.h"

bool platform_storage_load(rk_cfg_t *out) {
    // Your storage implementation
}

bool platform_storage_save(const rk_cfg_t *in) {
    // Your storage implementation
}
```

### 2. Required Implementations

| Header | Functions to Implement |
|--------|------------------------|
| `platform_storage.h` | `load`, `save`, `defaults`, `reset_wifi_only` |
| `platform_http.h` | `get`, `post`, `free` |
| `platform_mdns.h` | `init`, `discover_base_url` |
| `platform_input.h` | `init`, `process_events`, `shutdown` |
| `platform_time.h` | `millis`, `delay_ms` |
| `platform_log.h` | `LOGI`, `LOGW`, `LOGE` macros |

### 3. Display/LVGL Integration

The display is more complex. See `pc_sim/main_pc.c` for how the PC simulator integrates SDL2 with LVGL as an example of a non-ESP implementation.

---

## Hardware Abstraction Improvements

Current limitations that make porting harder:

1. **Pin definitions scattered** - Could centralize in a single `hardware_config.h`
2. **Display driver tightly coupled** - Could abstract display init behind interface
3. **Touch integrated with display** - Could separate touch as independent component
4. **No hardware detection** - Could auto-detect some components via I2C scan

These could be improved in future refactoring.
