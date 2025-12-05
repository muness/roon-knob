# BlueKnob Project Analysis

Analysis of [joshuacant/BlueKnob](https://github.com/joshuacant/BlueKnob) project for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8. This document identifies features and implementation patterns that may be useful for the Roon Knob project.

**Project Overview:**
- Bluetooth LE media controller (no WiFi)
- Optimized for battery operation with aggressive power management
- Deep sleep and hibernation modes
- Comprehensive battery monitoring

---

## 1. Battery Monitoring Implementation

### Hardware Configuration

**Identical to our hardware:**
```cpp
// From BlueKnob-ESP32S3/main/main.cpp
#define ADC_PIN GPIO_NUM_1           // ADC1_CH0
#define ADC_CHANNEL ADC_CHANNEL_0
#define ADC_ATTEN ADC_ATTEN_DB_12   // 0-3.3V range
#define ADC_WIDTH ADC_WIDTH_BIT_12  // 12-bit resolution (0-4095)

// Voltage divider calculation
voltage = 0.001 * mv_output * 2;    // 2.0x divider, same as demo firmware
```

### Raw ADC Thresholds

**Key insight: BlueKnob uses raw ADC values for thresholds, not converted voltage:**

```cpp
// From settings.h
#define BATT_MAX 2550  // Raw ADC value at full charge (~5.1V = USB/charging)
#define BATT_MIN 1600  // Raw ADC value at device shutdown (~3.2V)

// USB detection
plugged_in = (adc_raw_value > BATT_MAX);
```

**What this means:**
- When USB connected: ADC reads ~2550+ (measuring 4.9-5.1V charging circuit)
- On battery full charge: ADC reads ~2100 (4.2V battery)
- On battery low: ADC reads ~1600 (3.2V battery)
- Below 1600: Device should shutdown to protect battery

### Battery Percentage Calculation

**Uses raw ADC values directly for percentage lookup:**

```cpp
// Simplified from BlueKnob logic
int adc_raw = adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &adc_raw_value);

// Detect USB power
bool plugged_in = (adc_raw_value > BATT_MAX);

if (plugged_in) {
    battery_percent = 100;  // Show full when charging
} else {
    // Map ADC range to percentage
    if (adc_raw_value >= 2100) battery_percent = 100;      // 4.2V
    else if (adc_raw_value >= 2000) battery_percent = 90;  // 4.0V
    else if (adc_raw_value >= 1950) battery_percent = 80;  // 3.9V
    else if (adc_raw_value >= 1900) battery_percent = 70;  // 3.8V
    else if (adc_raw_value >= 1875) battery_percent = 60;  // 3.75V
    else if (adc_raw_value >= 1850) battery_percent = 50;  // 3.7V
    else if (adc_raw_value >= 1825) battery_percent = 40;  // 3.65V
    else if (adc_raw_value >= 1800) battery_percent = 30;  // 3.6V
    else if (adc_raw_value >= 1750) battery_percent = 20;  // 3.5V
    else if (adc_raw_value >= 1650) battery_percent = 10;  // 3.3V
    else battery_percent = 5;                               // Below 3.3V - critical
}
```

**Implementation for Roon Knob:**
```c
// In battery.c - add raw ADC threshold support
#define BATTERY_ADC_USB_THRESHOLD 2550   // Above this = USB connected
#define BATTERY_ADC_FULL 2100            // 4.2V fully charged
#define BATTERY_ADC_CRITICAL 1600        // 3.2V shutdown threshold

bool battery_is_usb_connected(void) {
    int raw_adc;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw_adc));
    return (raw_adc > BATTERY_ADC_USB_THRESHOLD);
}
```

---

## 2. Power Management Architecture

### Sleep Modes and Timeouts

**Three-tier timeout system:**

```cpp
// From settings.h
#define BACKLIGHT_DELAY 200              // 200ms - backlight fade time
#define DISPLAY_TIMEOUT 15000            // 15s - turn off screen, device stays awake
#define DEVICE_TIMEOUT 120000            // 2min - enter light sleep
#define DEVICE_DEEP_TIMEOUT 1800000      // 30min - enter deep sleep/hibernation
#define DEVICE_DEEP_TIMEOUT_US (DEVICE_DEEP_TIMEOUT * 1000)  // For esp_sleep_enable_timer_wakeup
```

**State Machine:**
1. **Active** (0-15s): Display on, full CPU, Bluetooth active
2. **Display Off** (15s-2min): Display blanked, knob/touch still responsive, instant wake
3. **Light Sleep** (2min-30min): CPU paused, Bluetooth connected, wakes on knob/touch
4. **Hibernation** (30min+): Deep sleep, Bluetooth disconnected, requires power button to wake

### Wake Sources Configuration

**GPIO wake from light/deep sleep:**

```cpp
// From settings.h
#define GPIO_WAKEUP_KNOB1 ENCODER_ECA_PIN  // GPIO8
#define GPIO_WAKEUP_KNOB2 ENCODER_ECB_PIN  // GPIO7
#define GPIO_WAKEUP_TOUCH PIN_NUM_TOUCH_INT // GPIO9
#define GPIO_WAKEUP_LEVEL 0                 // Wake on LOW (interrupt active low)

// Setup in main.cpp
void setup_gpio_wakeup(void) {
    // Configure encoder A pin
    esp_sleep_enable_ext0_wakeup(GPIO_WAKEUP_KNOB1, GPIO_WAKEUP_LEVEL);

    // Configure touch interrupt pin
    esp_sleep_enable_ext1_wakeup(
        (1ULL << GPIO_WAKEUP_TOUCH),
        ESP_EXT1_WAKEUP_ANY_LOW
    );

    // Configure timer wakeup for periodic checks
    esp_sleep_enable_timer_wakeup(DEVICE_DEEP_TIMEOUT_US);
}
```

### Display Power Management

**Backlight control with fade:**

```cpp
// Display timeout handling
static uint32_t last_activity_time = 0;

void check_display_timeout(void) {
    uint32_t idle_time = esp_timer_get_time() / 1000 - last_activity_time;

    if (idle_time > DISPLAY_TIMEOUT) {
        // Fade out backlight
        for (int i = current_brightness; i >= 0; i -= 5) {
            set_backlight_pwm(i);
            vTaskDelay(pdMS_TO_TICKS(BACKLIGHT_DELAY / (current_brightness / 5)));
        }
        display_on = false;
    }
}

void on_user_activity(void) {
    last_activity_time = esp_timer_get_time() / 1000;

    if (!display_on) {
        // Fade in backlight
        for (int i = 0; i <= saved_brightness; i += 5) {
            set_backlight_pwm(i);
            vTaskDelay(pdMS_TO_TICKS(BACKLIGHT_DELAY / (saved_brightness / 5)));
        }
        display_on = true;
    }
}
```

### CPU Frequency Scaling

**Dynamic frequency based on activity:**

```cpp
// When idle with display off
esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 160,  // Reduced from 240MHz
    .min_freq_mhz = 80,   // Idle frequency
    .light_sleep_enable = true
};
esp_pm_configure(&pm_config);
```

**For Roon Knob consideration:**
- WiFi requires higher CPU frequency than Bluetooth LE
- May not be able to drop to 80MHz while maintaining WiFi connection
- Consider 160MHz idle, 240MHz active as compromise

---

## 3. Bluetooth LE vs WiFi Power Comparison

### Current Draw Estimates

| Mode | Bluetooth LE (BlueKnob) | WiFi (Roon Knob) | Difference |
|------|------------------------|------------------|------------|
| **Active** | 15-25mA | 80-120mA | **4-6x more** |
| **Idle Connected** | 3-5mA | 30-50mA | **10x more** |
| **Light Sleep** | 1-2mA | 10-15mA | **10x more** |
| **Deep Sleep** | 10-20μA | 10-20μA | Same |

**WiFi power breakdown:**
- TX at 20dBm: 300-500mA peak
- TX at 11dBm: 150-200mA peak (BlueKnob equivalent)
- RX: 50-100mA continuous
- Beacon listening: 15mA average

**Bluetooth LE power breakdown:**
- TX: 10-15mA peak
- RX: 8-12mA during connection events
- Between events: 1-2mA
- Connection interval: 30ms typical (vs WiFi continuous)

### Battery Life Estimates (800mAh battery)

**BlueKnob (Bluetooth LE):**
- Active use: 32-53 hours
- Display off, connected: 160-267 hours (6-11 days)
- Light sleep: 400-800 hours (16-33 days)

**Roon Knob (WiFi):**
- Active use: 6-10 hours
- Display off, WiFi connected: 16-27 hours (less than 2 days)
- WiFi disconnected, light sleep: Similar to BlueKnob

**Implication:** WiFi fundamentally limits battery operation. Options:
1. Accept reduced battery life (6-10 hours active)
2. Disconnect WiFi during idle and reconnect on wake (adds 2-5s delay)
3. Add power source detection and change behavior based on USB vs battery

---

## 4. User Interface Patterns

### Activity Detection

**What counts as user activity:**
```cpp
// Reset idle timer on any of these:
void reset_idle_timer(void) {
    last_activity_time = esp_timer_get_time() / 1000;

    if (device_sleeping) {
        wake_device();
    }
    if (!display_on) {
        fade_in_display();
    }
}

// Called from:
- Touch screen interrupt handler
- Encoder rotation interrupt handler
- Button press events
```

### Battery Display

**Icon with percentage:**
```cpp
// LVGL battery indicator (top-left corner like Roon Knob)
lv_obj_t *battery_arc = lv_arc_create(screen);
lv_arc_set_range(battery_arc, 0, 100);
lv_arc_set_value(battery_arc, battery_percent);

// Color based on level
lv_color_t color;
if (plugged_in) {
    color = lv_color_hex(0x00FF00);  // Green when charging
} else if (battery_percent > 50) {
    color = lv_color_hex(0x00FF00);  // Green
} else if (battery_percent > 20) {
    color = lv_color_hex(0xFFA500);  // Orange
} else {
    color = lv_color_hex(0xFF0000);  // Red - low battery
}
lv_obj_set_style_arc_color(battery_arc, color, LV_PART_INDICATOR);

// Text overlay
lv_obj_t *battery_text = lv_label_create(screen);
lv_label_set_text_fmt(battery_text, "%d%%", battery_percent);
```

### Settings Screen

**Battery and power management settings:**
- Brightness slider (5-100%)
- Display timeout: 10s / 15s / 30s / 60s / Never
- Device sleep timeout: 1min / 2min / 5min / 10min / Never
- Battery stats: Current voltage, current draw estimate, time remaining
- WiFi toggle (if applicable)

---

## 5. Implementation Recommendations

### High Priority (Immediate)

**1. USB Power Detection**
```c
// Add to battery.c
bool battery_is_usb_connected(void) {
    int raw_adc;
    adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw_adc);
    return (raw_adc > 2550);  // Above 5.1V = USB power
}

// In main_idf.c - adjust behavior based on power source
if (battery_is_usb_connected()) {
    // USB powered - full performance
    esp_wifi_set_max_tx_power(78);  // 19.5 dBm
    esp_pm_configure(&pm_config_performance);
} else {
    // Battery powered - power saving
    esp_wifi_set_max_tx_power(44);  // 11 dBm
    esp_pm_configure(&pm_config_battery);
}
```

**2. Improved Battery Percentage Calculation**
```c
// Add to battery.c
int battery_get_percentage(void) {
    int raw_adc = battery_get_raw_adc();  // New function

    if (raw_adc > 2550) return 100;  // USB connected = "full"
    if (raw_adc >= 2100) return 100; // 4.2V
    if (raw_adc >= 2000) return 90;  // 4.0V
    if (raw_adc >= 1950) return 80;  // 3.9V
    if (raw_adc >= 1900) return 70;  // 3.8V
    if (raw_adc >= 1875) return 60;  // 3.75V
    if (raw_adc >= 1850) return 50;  // 3.7V
    if (raw_adc >= 1825) return 40;  // 3.65V
    if (raw_adc >= 1800) return 30;  // 3.6V
    if (raw_adc >= 1750) return 20;  // 3.5V
    if (raw_adc >= 1650) return 10;  // 3.3V - low warning
    if (raw_adc >= 1600) return 5;   // 3.2V - critical
    return 0;  // Below 3.2V - shutdown
}
```

**3. Display Timeout with Screen Blank**
```c
// Add idle timer to ui.c
static esp_timer_handle_t display_timeout_timer;

void display_timeout_callback(void *arg) {
    // Fade out display
    for (int i = current_brightness; i >= 0; i -= 5) {
        platform_set_backlight_brightness(i);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ui_reset_idle_timer(void) {
    if (backlight_off) {
        // Fade back in
        for (int i = 0; i <= saved_brightness; i += 5) {
            platform_set_backlight_brightness(i);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    esp_timer_stop(display_timeout_timer);
    esp_timer_start_once(display_timeout_timer, 15 * 1000000);  // 15s
}
```

### Medium Priority (Next Phase)

**4. Light Sleep with WiFi Connected**

Requires ESP-IDF WiFi sleep mode configuration:
```c
// In wifi_mgr.c after connection
esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // Modem sleep between DTIM beacons
```

**5. Settings Screen for Power Management**

Add LVGL screen with:
- Brightness slider
- Display timeout selector
- Battery voltage and percentage display
- WiFi power mode toggle

**6. Low Battery Warning**

Display warning overlay when battery < 20%:
```c
if (battery_percent <= 20 && !battery_is_usb_connected()) {
    ui_show_low_battery_warning();
}
if (battery_percent <= 5) {
    ui_show_critical_battery_message();
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_deep_sleep_start();  // Force shutdown to protect battery
}
```

### Low Priority (Future Enhancement)

**7. Deep Sleep Mode**

Not practical for Roon controller (needs to stay connected), but could be implemented for:
- Explicit "hibernate" command
- Critical low battery shutdown
- Long idle periods (user configurable)

**8. WiFi Disconnect on Idle**

Trade responsiveness for battery life:
```c
// After 2 minutes idle:
- Disconnect WiFi
- Enter light sleep
- Wake on knob/touch
- Reconnect WiFi (2-5s delay)
- Resume normal operation
```

---

## 6. Code Snippets

### Complete Battery Monitoring Task

**Adapted from BlueKnob main.cpp:**

```c
// In battery.c
static void battery_task(void *arg) {
    int raw_adc;
    float voltage;
    int percentage;
    bool usb_connected;

    while (1) {
        // Read raw ADC
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw_adc));

        // Convert to voltage
        int voltage_mv;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw_adc, &voltage_mv));
        voltage = (voltage_mv / 1000.0f) * 2.0f;  // 2.0x divider

        // Detect USB connection
        usb_connected = (raw_adc > 2550);

        // Calculate percentage
        percentage = battery_get_percentage_from_raw(raw_adc);

        // Update UI
        ui_update_battery_indicator(percentage, voltage, usb_connected);

        // Low battery check
        if (percentage <= 5 && !usb_connected) {
            ESP_LOGW(TAG, "Critical battery level: %d%% (%.2fV)", percentage, voltage);
            ui_show_low_battery_warning();

            if (percentage <= 1) {
                ESP_LOGE(TAG, "Shutting down to protect battery");
                ui_show_shutdown_message();
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_deep_sleep_start();
            }
        }

        // Check every 10 seconds (or 30s if on USB)
        vTaskDelay(pdMS_TO_TICKS(usb_connected ? 30000 : 10000));
    }
}
```

### Power Source Detection and WiFi Adjustment

**New pattern for Roon Knob:**

```c
// In main_idf.c or wifi_mgr.c
static bool last_usb_state = true;

void check_power_source_and_adjust(void) {
    bool usb_connected = battery_is_usb_connected();

    // Only adjust if state changed
    if (usb_connected != last_usb_state) {
        last_usb_state = usb_connected;

        if (usb_connected) {
            ESP_LOGI(TAG, "USB connected - enabling full performance mode");
            esp_wifi_set_max_tx_power(78);  // 19.5 dBm
            // TODO: Set CPU to 240MHz
            ui_set_message("USB Power");
        } else {
            ESP_LOGI(TAG, "On battery - enabling power saving mode");
            esp_wifi_set_max_tx_power(44);  // 11 dBm (saves ~50% WiFi power)
            // TODO: Set CPU to 160MHz
            ui_set_message("Battery Power");
        }
    }
}

// Call from main loop or timer callback every 10 seconds
```

---

## 7. Testing and Validation

### Battery Monitoring Tests

**Test Plan:**
1. **USB Connected:** Verify ADC reads 2500-2600 (4.9-5.2V)
2. **Full Battery:** Verify ADC reads ~2100 (4.2V), shows 100%
3. **Half Discharge:** Verify ADC reads ~1850 (3.7V), shows ~50%
4. **Low Battery:** Verify warning at 20% (1750 = 3.5V)
5. **Critical Battery:** Verify shutdown at 5% (1650 = 3.3V)
6. **USB Detection:** Verify `battery_is_usb_connected()` switches correctly

### Power Consumption Tests

**Measurement Setup:**
- USB power meter or multimeter in series with battery
- Measure current draw in different states
- Calculate runtime estimates

**Expected Results (with WiFi optimizations):**
- Display on, WiFi active: 80-120mA
- Display off, WiFi active: 30-50mA
- Display off, WiFi sleep mode: 15-25mA

**Current BlueKnob achieves (Bluetooth LE):**
- Display on, BLE active: 15-25mA
- Display off, BLE connected: 3-5mA
- Light sleep: 1-2mA

---

## 8. Key Differences: BlueKnob vs Roon Knob

| Feature | BlueKnob | Roon Knob | Impact |
|---------|----------|-----------|--------|
| **Connectivity** | Bluetooth LE | WiFi | 4-10x more power draw |
| **Use Case** | Generic media control | Roon-specific | Requires network |
| **Power Priority** | Battery first | Features first | Different tradeoffs |
| **Sleep Capability** | Deep sleep viable | Must stay connected | Limits power savings |
| **Connection Events** | 30ms intervals | Continuous polling | Continuous power draw |
| **Backend** | HID device profile | HTTP REST API | Different protocols |

**Conclusion:** We can adopt BlueKnob's battery monitoring and display timeout patterns, but cannot achieve the same battery life due to fundamental WiFi power requirements. Best case scenario with optimizations: **6-10 hours active use, 16-24 hours with display off.**

---

## 9. Related Documentation

**This Project:**
- `docs/reference/hardware/battery.md` - Hardware specifications
- `docs/reference/hardware/board.md` - GPIO pin assignments
- `idf_app/main/battery.c` - Current battery driver implementation

**External Projects:**
- [BlueKnob GitHub](https://github.com/joshuacant/BlueKnob)
- [WaveShare-Knob-Esp32S3](https://github.com/KrX3D/WaveShare-Knob-Esp32S3) - ESPHome configs
- [ESP32-S3 Power Management API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/power_management.html)
- [ESP32-S3 Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/sleep_modes.html)

---

## 10. Action Items for Roon Knob

### Immediate Fixes (Resolve Boot on Battery)
- [x] Correct voltage divider to 2.0x - DONE
- [x] Lower brownout detection to level 4 (2.50V) - DONE
- [ ] Test boot on battery after demo charges fully

### Phase 1: Basic Battery Operation
- [ ] Implement `battery_is_usb_connected()` using ADC threshold
- [ ] Implement `battery_get_percentage()` using raw ADC lookup table
- [ ] Update UI battery indicator with percentage and USB icon
- [ ] Add low battery warning (20%) and critical shutdown (5%)
- [ ] Reduce WiFi TX power to 11dBm when on battery

### Phase 2: Power Optimization
- [ ] Implement display timeout (15s) with backlight fade
- [ ] Enable WiFi modem sleep mode (`WIFI_PS_MIN_MODEM`)
- [ ] Dynamic CPU frequency based on USB vs battery
- [ ] Settings screen for brightness and timeout configuration

### Phase 3: Advanced Power Management
- [ ] Light sleep when display off (if WiFi allows)
- [ ] WiFi disconnect/reconnect on long idle (user configurable)
- [ ] Battery statistics screen (voltage, current, time remaining)
- [ ] Optional: Hibernate mode after extended idle

**Estimated implementation time:** Phase 1 = 1-2 days, Phase 2 = 2-3 days, Phase 3 = 3-5 days
