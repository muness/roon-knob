# Implementation Notes

Technical documentation for ESP32-S3 knob-style applications.

## Architecture Overview

The firmware uses a layered architecture with platform abstraction:

```
┌─────────────────────────────────────────┐
│         Application Layer               │
│  (app logic, state management)          │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│         Common UI Layer                  │
│  (ui.c, LVGL-based rendering)           │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│      Platform Abstraction Layer         │
│  (platform_*.c interfaces)              │
└──────────────┬──────────────────────────┘
               │
       ┌───────┴────────┐
       │                │
┌──────▼──────┐  ┌──────▼──────┐
│   PC Sim    │  │   ESP32-S3  │
│  (SDL2)     │  │  (ESP-IDF)  │
└─────────────┘  └─────────────┘
```

## Input System

### Hardware Inputs

1. **Rotary encoder** - Volume/scroll control
2. **Touchscreen** - All button interactions (play/pause, menus, etc.)

**Note:** This board has NO physical buttons. The encoder cannot be pressed.

### Rotary Encoder (ESP32-S3)

**File:** `idf_app/main/platform_input_idf.c`

**Implementation:** Software quadrature decoding with polling

| Parameter | Value |
|-----------|-------|
| GPIO A | 8 |
| GPIO B | 7 |
| Poll interval | 3ms |
| Debounce | 2 consecutive stable reads |
| Pull-ups | Internal enabled |

**Signal flow:**
```
Encoder rotation
    ↓
Software quadrature decode (polling)
    ↓
Debounce filter
    ↓
Calculate direction
    ↓
ui_dispatch_input(UI_INPUT_VOL_UP/DOWN)
```

**Why software polling instead of PCNT:**
- Simpler implementation
- Adequate for UI knob speeds
- Portable to other platforms
- Lower complexity

### Touch Input (ESP32-S3)

**File:** `idf_app/main/platform_display_idf.c`

**Implementation:** CST816D via I2C, integrated with LVGL

| Parameter | Value |
|-----------|-------|
| I2C Address | 0x15 |
| I2C Speed | 300 kHz |
| GPIO SDA | 11 |
| GPIO SCL | 12 |

Touch is registered as an LVGL `LV_INDEV_TYPE_POINTER` device. LVGL handles:
- Tap detection
- Drag/swipe
- Widget interaction

### PC Simulator Input

**File:** `pc_sim/platform_input_pc.c`

Uses SDL keyboard/mouse events:
- Arrow keys / Mouse wheel → Volume control
- Space / Enter → Play/pause
- Mouse click → Touch simulation

### Input Event Types

Defined in `common/ui.h`:
```c
typedef enum {
    UI_INPUT_VOL_DOWN = -1,
    UI_INPUT_NONE = 0,
    UI_INPUT_VOL_UP = 1,
    UI_INPUT_PLAY_PAUSE = 2,
    UI_INPUT_MENU = 3,
} ui_input_event_t;
```

## Display System

### ESP32-S3 Hardware Display

**File:** `idf_app/main/platform_display_idf.c`

| Parameter | Value |
|-----------|-------|
| Controller | SH8601 |
| Resolution | 360×360 |
| Interface | QSPI (4-wire) |
| Color format | RGB565 |
| SPI Host | SPI2_HOST |

**Critical: Byte Order**

The SH8601 expects big-endian RGB565, but ESP32-S3 is little-endian. The flush callback swaps bytes before sending:

```c
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // Swap bytes for big-endian display
    uint16_t *pixels = (uint16_t *)px_map;
    for (int i = 0; i < pixel_count; i++) {
        pixels[i] = (pixels[i] >> 8) | (pixels[i] << 8);
    }
    esp_lcd_panel_draw_bitmap(...);
}
```

See [COLORTEST_HELLOWORLD.md](../esp/hw-reference/COLORTEST_HELLOWORLD.md) for details.

**Initialization sequence:**
1. Initialize SPI bus
2. Configure LCD panel IO (QSPI)
3. Load SH8601 init commands
4. Register LVGL display driver with byte-swap flush callback
5. Register touch input device
6. Start LVGL tick timer

### PC Simulator Display

**File:** `pc_sim/main_pc.c`

Uses SDL2 for rendering:
- Window size: 360×360
- LVGL driver: SDL window/renderer

## Memory Layout (ESP32-S3)

### Flash (16 MB)
- Bootloader: ~32 KB
- Partition table: 4 KB
- Factory app: ~2 MB
- OTA_0: ~2 MB
- OTA_1: ~2 MB
- NVS: 24 KB
- SPIFFS: ~8 MB (for assets)

### RAM
- **Internal SRAM (512 KB):**
  - DMA buffers (display transfer)
  - Task stacks
  - Critical data structures

- **PSRAM (8 MB):**
  - LVGL draw buffers
  - Artwork/image cache
  - Network response buffers
  - General heap

## Threading Model

### ESP32-S3

| Task | Purpose | Priority |
|------|---------|----------|
| Main task | App initialization, UI loop | Normal |
| Network task | HTTP polling | Normal |
| LVGL tick timer | Time tracking | Timer (ISR) |
| Input poll timer | Encoder sampling | Timer (ISR) |

**Synchronization:**
- UI state: Mutex-protected updates
- Input events: Direct callback (LVGL-safe)
- Network state: Atomic flags

### PC Simulator
- Main thread: SDL event loop + LVGL loop
- Network thread: HTTP polling

## Build System

### ESP32-S3
```bash
cd idf_app
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

**Dependencies:** ESP-IDF v5.x+, LVGL, esp_lcd_sh8601

### PC Simulator
```bash
cmake -B build/pc_sim pc_sim
cmake --build build/pc_sim
./build/pc_sim/roon_knob_pc
```

**Dependencies:** SDL2, libcurl, LVGL (fetched by CMake)

## Platform Abstraction

The `platform_*.h` headers define interfaces implemented differently per platform:

| Interface | ESP32-S3 | PC Sim |
|-----------|----------|--------|
| `platform_display` | SH8601/QSPI | SDL2 |
| `platform_input` | Encoder + Touch | Keyboard/Mouse |
| `platform_http` | esp_http_client | libcurl |
| `platform_storage` | NVS | JSON file |
| `platform_wifi` | ESP WiFi | N/A (uses host) |

## Implementation Files

### Core
- `common/ui.c` - LVGL UI layout and state
- `common/ui.h` - UI interface and event types

### ESP32-S3 Platform
- `idf_app/main/platform_display_idf.c` - Display + touch
- `idf_app/main/platform_input_idf.c` - Rotary encoder
- `idf_app/main/platform_battery_idf.c` - Battery ADC
- `idf_app/main/platform_http_idf.c` - HTTP client
- `idf_app/main/platform_storage_idf.c` - NVS storage
- `idf_app/main/platform_wifi_idf.c` - WiFi management

### PC Simulator
- `pc_sim/main_pc.c` - SDL setup and main loop
- `pc_sim/platform_input_pc.c` - Keyboard input
- `pc_sim/platform_display_pc.c` - SDL display
