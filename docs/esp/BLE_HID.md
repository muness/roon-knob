# BLE HID (Bluetooth Media Control)

> **Note**: This project uses the dedicated ESP32 chip for Bluetooth instead of S3 BLE.
> See [DUAL_CHIP_ARCHITECTURE.md](../usage/DUAL_CHIP_ARCHITECTURE.md) for the current implementation.
> The documentation below is preserved for developers who want to implement BLE HID on ESP32-S3.

---

The Roon Knob can operate as a Bluetooth HID (Human Interface Device) media controller, allowing it to control any Bluetooth-enabled device without requiring WiFi or the Roon ecosystem.

## Overview

When Bluetooth mode is selected from the zone picker, the knob:
1. Stops WiFi completely (BLE and WiFi share the radio)
2. Starts advertising as "Roon Knob"
3. Accepts pairing from any Bluetooth device
4. Sends HID Consumer Control commands for media control

## Supported Controls

| Input | HID Command | Function |
|-------|-------------|----------|
| Rotate clockwise | Volume Up | Increase volume |
| Rotate counter-clockwise | Volume Down | Decrease volume |
| Tap center | Play + Pause | Toggle playback |
| Tap left | Scan Previous Track | Previous track |
| Tap right | Scan Next Track | Next track |

## Pairing

1. Select "Bluetooth" from the zone picker (long-press zone name)
2. On your phone/computer, go to Bluetooth settings
3. Find "Roon Knob" and tap to pair
4. Uses "Just Works" pairing - no PIN required
5. Bond is saved - device will auto-reconnect after reboot

## Technical Implementation

### BLE Stack

Uses a custom BLE HID profile based on [BlueKnob](https://github.com/peterramsing/BlueKnob)'s proven implementation:

- `idf_app/components/ble_hid/` - HID device profile (GATT services)
- `idf_app/main/ble_hid_client.c` - High-level API and state management

### HID Report Descriptor

The HID profile advertises as a Consumer Control device with the following capabilities:
- Volume Up/Down (Usage Page 0x0C, Usage 0xE9/0xEA)
- Play/Pause (Usage 0xCD)
- Scan Next/Previous Track (Usage 0xB5/0xB6)

### Memory Configuration

BLE and WiFi both require significant RAM. The following optimizations are applied in `sdkconfig.defaults`:

```
# WiFi buffer reduction
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16

# BT memory settings
CONFIG_BT_BTC_TASK_STACK_SIZE=3072
CONFIG_BT_BTU_TASK_STACK_SIZE=4096
CONFIG_BT_GATT_MAX_SR_PROFILES=4
CONFIG_BT_GATT_MAX_SR_ATTRIBUTES=50
```

### Radio Coexistence

ESP32-S3 has a single radio shared between WiFi and BLE. While they can technically coexist, initialization conflicts can cause crashes. Our approach:

1. **WiFi mode (default)**: WiFi runs, BLE disabled
2. **Switching to BLE**: `wifi_mgr_stop()` fully deinitializes WiFi before starting BLE
3. **Switching to WiFi**: `ble_hid_client_stop()` deinitializes BLE before starting WiFi

### State Management

```c
typedef enum {
    BLE_HID_STATE_DISABLED,    // BLE not started
    BLE_HID_STATE_ADVERTISING, // Waiting for connection
    BLE_HID_STATE_CONNECTED    // Device connected, can send commands
} ble_hid_state_t;
```

The UI shows different status for each state:
- Disabled: Shows WiFi/Roon status
- Advertising: Shows "BLE: Advertising..."
- Connected: Shows "BLE: Connected"

### Controller Mode Persistence

The selected mode (Roon zones or Bluetooth) is saved to NVS:

```c
// common/controller_mode.c
void controller_mode_set(controller_mode_t mode);  // Saves to NVS
controller_mode_t controller_mode_get(void);       // Loads from NVS
```

On boot, the saved mode is restored. If Bluetooth was selected, WiFi initialization is skipped entirely.

## Kconfig Options

In `idf_app/main/Kconfig.projbuild`:

```
CONFIG_ROON_KNOB_BLE_HID_ENABLED=y   # Enable BLE HID support
CONFIG_ROON_KNOB_BLE_DEVICE_NAME     # Advertised device name (default: "Roon Knob")
```

To disable BLE support entirely (saves ~100KB flash):
```
CONFIG_ROON_KNOB_BLE_HID_ENABLED=n
```

## Debugging

Enable verbose BLE logging:
```
CONFIG_BT_LOG_HCI_TRACE_LEVEL_VERBOSE=y
CONFIG_BT_LOG_BTM_TRACE_LEVEL_VERBOSE=y
```

Key log tags:
- `ble_hid` - High-level state changes and commands
- `HID_LE_PRF` - GATT profile events
- `BLE_INIT` - Controller initialization

## Limitations

1. **No simultaneous WiFi+BLE**: Only one can be active at a time
2. **Single connection**: Only one device can connect at a time
3. **No media info**: BLE HID is output-only; can't receive track info from the connected device
4. **iOS quirks**: Some iOS versions require unpairing/repairing after firmware updates

---

## Porting Guide

### Using BLE HID in Your Own ESP32 Project

The `ble_hid` component is self-contained and can be copied to other ESP-IDF projects:

1. **Copy the component**: `idf_app/components/ble_hid/` → your project's `components/`

2. **Add to CMakeLists.txt**:
   ```cmake
   PRIV_REQUIRES bt ble_hid
   ```

3. **Add sdkconfig.defaults**:
   ```
   CONFIG_BT_ENABLED=y
   CONFIG_BT_BLE_ENABLED=y
   CONFIG_BT_BLUEDROID_ENABLED=y
   CONFIG_BT_CLASSIC_ENABLED=n
   CONFIG_BT_GATTS_ENABLE=y
   CONFIG_BT_BLE_SMP_ENABLE=y
   ```

4. **Initialize and use**:
   ```c
   #include "esp_hidd_prf_api.h"
   #include "hid_dev.h"

   // Initialize BLE stack (see ble_hid_client_start() for full sequence)
   esp_bt_controller_init(&bt_cfg);
   esp_bt_controller_enable(ESP_BT_MODE_BLE);
   esp_bluedroid_init();
   esp_bluedroid_enable();
   esp_hidd_profile_init();

   // Register callbacks
   esp_ble_gap_register_callback(gap_event_handler);
   esp_hidd_register_callbacks(hidd_event_callback);

   // Send HID commands (after connection established)
   esp_hidd_send_consumer_value(conn_id, HID_CONSUMER_VOLUME_UP, true);
   esp_hidd_send_consumer_value(conn_id, HID_CONSUMER_VOLUME_UP, false);
   ```

### Available HID Consumer Commands

From `hid_dev.h`:

| Constant | Value | Function |
|----------|-------|----------|
| `HID_CONSUMER_POWER` | 48 | Power |
| `HID_CONSUMER_PLAY` | 176 | Play |
| `HID_CONSUMER_PAUSE` | 177 | Pause |
| `HID_CONSUMER_RECORD` | 178 | Record |
| `HID_CONSUMER_FAST_FORWARD` | 179 | Fast Forward |
| `HID_CONSUMER_REWIND` | 180 | Rewind |
| `HID_CONSUMER_SCAN_NEXT_TRK` | 181 | Next Track |
| `HID_CONSUMER_SCAN_PREV_TRK` | 182 | Previous Track |
| `HID_CONSUMER_STOP` | 183 | Stop |
| `HID_CONSUMER_PLAY_PAUSE` | 205 | Play/Pause Toggle |
| `HID_CONSUMER_MUTE` | 226 | Mute |
| `HID_CONSUMER_VOLUME_UP` | 233 | Volume Up |
| `HID_CONSUMER_VOLUME_DOWN` | 234 | Volume Down |

### Building a Different App on This Hardware

To create a BLE-only app for the Waveshare ESP32-S3 Knob:

1. **Start from `ble_hid_client.c`** as your main control logic
2. **Remove Roon/WiFi code** - delete `roon_client.c`, `wifi_manager.c`, etc.
3. **Simplify `main_idf.c`** - just init display, input, and BLE
4. **Modify the UI** - `ui.c` can be simplified to just show BLE status

Example minimal main:
```c
void app_main(void) {
    nvs_flash_init();
    platform_display_init();
    lv_init();
    platform_display_register_lvgl_driver();
    platform_input_init();

    // Your UI init
    ui_init_ble_only();

    // Start BLE immediately
    ble_hid_client_start();
    ble_hid_client_set_state_callback(my_state_callback);

    // Main loop
    while (1) {
        platform_input_process_events();
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Porting to Different ESP32 Hardware

The BLE HID component has no hardware dependencies - it works on any ESP32 variant with BLE support:
- ESP32 (original)
- ESP32-S3 (this project)
- ESP32-C3
- ESP32-C6

Just ensure your `sdkconfig` matches the chip's BLE capabilities.
