# Boot Sequence and Initialization

This document covers the firmware startup sequence and the deferred-operation pattern used for thread safety.

## Overview

The ESP32-S3 firmware has a carefully ordered initialization sequence. Components must be initialized in a specific order due to dependencies between subsystems.

## Boot Sequence Diagram

```
app_main()
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 1. NVS Flash Init                                              │
│    - Initialize non-volatile storage                           │
│    - Erase and reinit if corrupted                             │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 2. Display Hardware Init                                       │
│    - SPI bus initialization                                    │
│    - SH8601 LCD panel driver                                   │
│    - I2C bus (touch controller, haptic motor)                  │
│    - CST816 touch controller                                   │
│    - PWM backlight                                             │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 3. Battery Monitoring Init                                     │
│    - ADC calibration                                           │
│    - Voltage reading task                                      │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 4. OTA Module Init                                             │
│    - Read current firmware version                             │
│    - Initialize status tracking                                │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 5. LVGL Library Init                                           │
│    - lv_init() - core library                                  │
│    - MUST be after display hardware init                       │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 6. LVGL Display Driver Registration                            │
│    - Create LVGL display object                                │
│    - Allocate DMA draw buffers                                 │
│    - Register flush callback                                   │
│    - Register touch input device                               │
│    - Start LVGL tick timer (2ms)                               │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 7. UI Init                                                     │
│    - Create LVGL widgets                                       │
│    - Set up event handlers                                     │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 8. Input Init                                                  │
│    - Configure rotary encoder GPIOs                            │
│    - Create input event queue                                  │
│    - Start polling timer (3ms)                                 │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 9. UI Loop Task Creation                        ◄── CRITICAL   │
│    - xTaskCreate(ui_loop_task, 8KB stack, priority 2)          │
│    - MUST be before WiFi starts                                │
│    - WiFi events use lv_async_call (needs task running)        │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 10. Display Sleep Init                                         │
│    - Create dim/sleep timers                                   │
│    - Requires UI task handle for priority control              │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 11. App Entry                                                  │
│    - Load configuration from NVS                               │
│    - Start Roon client polling                                 │
│    - Set initial UI state                                      │
└────────────────────────────────────────────────────────────────┘
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│ 12. WiFi Start                                  ◄── LAST       │
│    - Initialize WiFi stack                                     │
│    - Register event handlers                                   │
│    - Begin connection (STA) or provisioning (AP)               │
└────────────────────────────────────────────────────────────────┘
```

## Why Order Matters

### Display Before LVGL

```c
// WRONG: LVGL init before display hardware
lv_init();                    // ✗ No display to attach
platform_display_init();      // Too late

// CORRECT: Hardware first, then LVGL
platform_display_init();      // ✓ Set up SPI, LCD panel
lv_init();                    // ✓ Now can create display object
platform_display_register_lvgl_driver();  // ✓ Attach to LVGL
```

### UI Task Before WiFi

```c
// WRONG: WiFi before UI task
wifi_mgr_start();             // ✗ WiFi events fire immediately
xTaskCreate(ui_loop_task...); // Too late - events lost or crash

// CORRECT: UI task first
xTaskCreate(ui_loop_task...); // ✓ Task running, LVGL ready
wifi_mgr_start();             // ✓ Events can use lv_async_call
```

WiFi events call `lv_async_call()` to safely update the UI from the system event handler context. This requires the LVGL task to be running.

## Deferred Operations Pattern

### The Problem

ESP-IDF event handlers (WiFi, timers, ISRs) run in contexts with limited stack space. Heavy operations like mDNS initialization, HTTP requests, or OTA checks can overflow the stack.

### The Solution

Set flags in event handlers, process them in the main UI loop:

```c
// Flags for deferred operations
static volatile bool s_ota_check_pending = false;
static volatile bool s_config_server_start_pending = false;
static volatile bool s_mdns_init_pending = false;

// In WiFi event handler (limited stack ~3KB)
void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    if (evt == RK_NET_EVT_GOT_IP) {
        // DON'T do heavy work here:
        // platform_mdns_init();     ✗ Stack overflow risk
        // ota_check_for_update();   ✗ Stack overflow risk

        // DO set flags for later:
        s_mdns_init_pending = true;  // ✓ Safe
        s_ota_check_pending = true;  // ✓ Safe
    }
}

// In UI loop task (8KB stack, safe for heavy work)
static void ui_loop_task(void *arg) {
    while (true) {
        // Process deferred operations
        if (s_mdns_init_pending) {
            s_mdns_init_pending = false;
            platform_mdns_init(NULL);  // ✓ Plenty of stack
        }
        if (s_ota_check_pending) {
            s_ota_check_pending = false;
            ota_check_for_update();    // ✓ Plenty of stack
        }

        ui_loop_iter();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Current Deferred Operations

| Flag | Set By | Action |
|------|--------|--------|
| `s_mdns_init_pending` | WiFi GOT_IP event | Initialize mDNS service |
| `s_ota_check_pending` | WiFi GOT_IP event | Check for firmware updates |
| `s_config_server_start_pending` | WiFi GOT_IP event | Start HTTP config server |
| `s_config_server_stop_pending` | WiFi AP_STARTED event | Stop config server in AP mode |
| `s_pending_art_mode` | Touch callback | Enter art mode (swipe up) |
| `s_pending_exit_art_mode` | Touch callback | Exit art mode (swipe down) |
| `s_pending_dim` | Timer callback | Dim display backlight |
| `s_pending_sleep` | Timer callback | Turn off display |

## UI Loop Task

The main UI loop runs at approximately 100Hz (10ms delay):

```c
static void ui_loop_task(void *arg) {
    while (true) {
        // 1. Process input events from encoder (ISR context → queue)
        platform_input_process_events();

        // 2. Process pending display actions (gestures, sleep)
        platform_display_process_pending();

        // 3. Run LVGL task handler (rendering, animations)
        ui_loop_iter();

        // 4. Check OTA status (every 500ms)
        if (++counter >= 50) {
            check_ota_status();
            counter = 0;
        }

        // 5. Process deferred WiFi operations
        if (s_mdns_init_pending) { ... }
        if (s_ota_check_pending) { ... }

        // 6. Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Task Configuration

| Parameter | Value | Reason |
|-----------|-------|--------|
| Stack Size | 8KB | LVGL themes need extra space |
| Priority | 2 | Above idle (1), below WiFi (5) |
| Core | Any | Not pinned to specific core |

## NVS Recovery

On boot, NVS is checked for corruption:

```c
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
    err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition is corrupted or incompatible
    nvs_flash_erase();  // Wipe it
    nvs_flash_init();   // Reinitialize
}
```

This handles:
- **NVS_NO_FREE_PAGES**: Flash wear leveling exhausted
- **NVS_NEW_VERSION_FOUND**: NVS format changed between IDF versions

After recovery, all stored configuration is lost and defaults are applied.

## Error Handling

### Critical Failures (Abort)

```c
// Display init failure - can't continue without UI
if (!platform_display_init()) {
    ESP_LOGE(TAG, "Display hardware init failed!");
    return;  // app_main returns, system halts
}
```

### Non-Critical Failures (Continue)

```c
// Battery monitoring - nice to have, not essential
if (!battery_init()) {
    ESP_LOGW(TAG, "Battery monitoring init failed, continuing without it");
}
```

## Adding New Initialization

When adding new subsystems:

1. **Determine dependencies** - What must be initialized first?
2. **Choose stack context** - Does it need heavy stack? Use deferred pattern.
3. **Add to sequence** - Insert at appropriate point in `app_main()`
4. **Log initialization** - Use `ESP_LOGI(TAG, "Initializing X...")`

Example adding a new sensor:

```c
// In app_main(), after battery init:
ESP_LOGI(TAG, "Initializing accelerometer...");
if (!accel_init()) {
    ESP_LOGW(TAG, "Accelerometer init failed, continuing without it");
}
```

If the sensor needs network:

```c
// Add flag
static volatile bool s_sensor_cloud_sync_pending = false;

// In WiFi event handler
case RK_NET_EVT_GOT_IP:
    s_sensor_cloud_sync_pending = true;
    break;

// In UI loop
if (s_sensor_cloud_sync_pending) {
    s_sensor_cloud_sync_pending = false;
    sensor_sync_to_cloud();  // Safe - 8KB stack available
}
```

## Implementation Files

| File | Purpose |
|------|---------|
| `main_idf.c` | `app_main()`, UI loop task, event callbacks |
| `app_main.c` | `app_entry()`, application-level init |
| `platform_display_idf.c` | Display hardware and LVGL driver |
| `platform_input_idf.c` | Rotary encoder input |
| `wifi_manager.c` | WiFi STA/AP management |
| `display_sleep.c` | Sleep timer management |
