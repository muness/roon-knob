# Smart Knob Project Analysis

Analysis of [jennellew/smart-knob](https://github.com/jennellew/smart-knob) - an Arduino-based smart home controller for the same Waveshare ESP32-S3-Knob-Touch-LCD-1.8 hardware.

**Project Overview:**
- **Language:** Arduino (C/C++) - 80% C, 20% C++
- **Hardware:** Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (same as Roon Knob)
- **Purpose:** Control Kasa smart home devices (bulbs, plugs) via physical knob
- **UI Framework:** LVGL with Squareline Studio
- **Network:** WiFi with Kasa protocol

---

## 1. Display Sleep/Power Management

**Most Useful Feature for Roon Knob**

### Implementation Pattern

Uses a two-stage timeout system:
- **15 seconds**: Dim backlight from 40% to 10%
- **30 seconds**: Turn off display completely + lower LVGL task priority

```c
// display_sleep.c
#define DISPLAY_DIM_TIMEOUT_MS 15000    // 15 seconds
#define DISPLAY_SLEEP_TIMEOUT_MS 30000  // 30 seconds

#define BACKLIGHT_NORMAL 40
#define BACKLIGHT_DIM 10

#define LVGL_TASK_PRIORITY_NORMAL 2
#define LVGL_TASK_PRIORITY_LOW 1
```

### Key Functions

```c
void display_sleep_init(esp_lcd_panel_handle_t panel_handle,
                       TaskHandle_t lvgl_task_handle);
void display_dim(void);              // Called after 15s idle
void display_sleep(void);            // Called after 30s idle
void display_wake(void);             // Called on any activity
void display_activity_detected(void); // Call from touch/encoder callbacks
```

### What Happens on Sleep

1. **Backlight off** (`setUpdutySubdivide(0)`)
2. **Display panel off** (`esp_lcd_panel_disp_on_off(panel, false)`)
3. **LVGL priority lowered** (`vTaskPrioritySet(lvgl_task, PRIORITY_LOW)`)
4. **Timers stopped**

### What Happens on Wake

1. **Display panel on** (`esp_lcd_panel_disp_on_off(panel, true)`)
2. **10ms stabilization delay**
3. **LVGL priority restored** to normal
4. **Backlight on** (40%)
5. **Timers restarted**

### Thread Safety

Uses FreeRTOS mutex (`display_state_mutex`) to protect state changes:
```c
LOCK_DISPLAY_STATE();
// ... state changes ...
UNLOCK_DISPLAY_STATE();
```

### Integration Points

Every input callback calls `display_activity_detected()`:
```c
static void _knob_left_cb(void *arg, void *data) {
    display_activity_detected();  // Always wake/reset timer
    // ... handle knob input ...
}
```

**For Roon Knob:**
- Add `display_activity_detected()` to touch callback
- Add to encoder callback
- Add to button press handlers
- Configure timeouts based on use case (maybe longer for music listening)

---

## 2. Encoder/Knob Handling

**Uses Espressif's bidirectional switch/knob component**

### Hardware Configuration

```c
// knob.c
#define EXAMPLE_ENCODER_ECA_PIN    8  // Same as Roon Knob
#define EXAMPLE_ENCODER_ECB_PIN    7  // Same as Roon Knob
```

### Abstraction Layer Pattern

**High-level API** (`knob.h`):
```c
void knob_init(void);
void knob_activate(void);        // Enable encoder value updates
void knob_deactivate(void);      // Ignore encoder (still wakes display)
bool knob_is_active(void);
int8_t knob_get_value(void);     // Get current value (0-100)
void knob_set_value(int8_t value);
void knob_set_value_changed_callback(void (*callback)(int8_t));
```

**Low-level implementation** (`bidi_switch_knob.c/h`):
- Quadrature decoding
- Debouncing
- Event generation (KNOB_LEFT, KNOB_RIGHT)

### Usage Pattern

```c
// Setup
knob_init();
knob_set_value(50);  // Initial brightness
knob_activate();
knob_set_value_changed_callback(on_brightness_changed);

// Callback receives new value
void on_brightness_changed(int8_t new_value) {
    ESP_LOGI(TAG, "Brightness: %d%%", new_value);
    bulb->setBrightness(new_value);
    lv_slider_set_value(brightness_slider, new_value, LV_ANIM_OFF);
}
```

### Value Range

- 0-100 range (percentage)
- Step size: 5 (each click = ±5%)
- Clamped automatically

### Encoder State Machine

- **Inactive**: Encoder rotation still wakes display, but doesn't change value
- **Active**: Rotation changes value and triggers callback
- Useful for context-sensitive behavior (e.g., disable during zone selection)

**For Roon Knob:**
- We already have basic encoder support
- Consider adding activate/deactivate for UI contexts
- Add value range abstraction (currently we send raw events)

---

## 3. Touch Controller (CST816)

**Uses OLD I2C API** (`driver/i2c.h` not `driver/i2c_master.h`)

```c
// cst816.cpp
void Touch_Init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,  // GPIO 11
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,  // GPIO 12
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = 300 * 1000},  // 300 kHz
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0));

    // Switch to normal mode
    uint8_t data = 0x00;
    I2C_writr_buff(TOUCH_ADDR, 0x00, &data, 1);
}

uint8_t getTouch(uint16_t *x, uint16_t *y) {
    uint8_t data[7] = {0};
    I2C_read_buff(TOUCH_ADDR, 0x00, data, 7);
    uint8_t num_touches = data[2];

    if (num_touches) {
        *x = ((uint16_t)(data[3] & 0x0f) << 8) + (uint16_t)data[4];
        *y = ((uint16_t)(data[5] & 0x0f) << 8) + (uint16_t)data[6];
        return 1;
    }
    return 0;
}
```

**Not useful** - We're already using BlueKnob's newer I2C master API implementation.

---

## 4. Arduino vs ESP-IDF Comparison

### Smart Knob (Arduino)

**Pros:**
- Simple setup() and loop() structure
- LVGL integration via Arduino libraries
- Quick prototyping

**Cons:**
- Old I2C API
- Less control over FreeRTOS tasks
- No component-based architecture

**Code:**
```cpp
void setup() {
    Serial.begin(115200);
    knob_init();
    Touch_Init();
    lcd_lvgl_Init();
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_50);
    wifi_init();
    kasa_device_init();
}

void loop() {
    lv_timer_handler();
    ui_tick();  // LVGL updates
}
```

### Roon Knob (ESP-IDF)

**Pros:**
- New I2C master API
- Component-based architecture
- Better task management
- Professional build system

**Cons:**
- More boilerplate
- Steeper learning curve

**Our approach is better** for a production device, but we can borrow their patterns.

---

## 5. Useful Patterns to Adopt

### Priority 1: Display Sleep Management

**Implement immediately** - most valuable feature

1. Add `display_sleep.c/h` files (copy/adapt from smart-knob)
2. Initialize in `platform_display_idf.c`:
   ```c
   display_sleep_init(panel_handle, lvgl_task_handle);
   ```
3. Call `display_activity_detected()` from:
   - Touch callback (LVGL input driver)
   - Encoder callback (`platform_input_idf.c`)
   - Button press handlers

4. Configure timeouts:
   - Music listening: Maybe 30s dim, 60s sleep?
   - Browsing zones: 15s dim, 30s sleep?
   - Make configurable in settings

### Priority 2: Encoder Value Abstraction

**Current approach:**
```c
// We send raw events
ui_dispatch_input(UI_INPUT_VOL_UP);
ui_dispatch_input(UI_INPUT_VOL_DOWN);
```

**Better approach** (like smart-knob):
```c
// Maintain 0-100 value, expose via API
int8_t encoder_get_volume(void);
void encoder_set_volume(int8_t vol);
void encoder_set_volume_callback(void (*cb)(int8_t));

// In Roon code
void on_volume_changed(int8_t new_vol) {
    roon_set_volume(new_vol);
    ui_update_volume_display(new_vol);
}
```

Benefits:
- Decouples encoder from UI
- Can set value programmatically (from Roon API updates)
- Cleaner API

### Priority 3: Context-Sensitive Encoder

Use activate/deactivate pattern:
```c
// When browsing zones:
encoder_deactivate();  // Don't change volume
// Still wakes display on rotation

// When on now-playing screen:
encoder_activate();    // Volume control active
```

### Priority 4: Thread-Safe State Management

Use mutex pattern for display state:
```c
static SemaphoreHandle_t g_display_mutex;
#define LOCK_DISPLAY() xSemaphoreTake(g_display_mutex, portMAX_DELAY)
#define UNLOCK_DISPLAY() xSemaphoreGive(g_display_mutex)
```

Prevents race conditions between:
- LVGL task
- Sleep timers
- Touch callbacks
- Encoder callbacks

---

## 6. What NOT to Adopt

### Arduino Framework
Stay with ESP-IDF - better control and newer APIs

### Old I2C API
BlueKnob's `i2c_master` API is better than smart-knob's old `driver/i2c.h`

### Kasa Protocol
Not relevant - we use Roon API

### Squareline Studio Workflow
Smart-knob uses Squareline, but so does BlueKnob. Not unique to this project.

---

## 7. Implementation Checklist

### Phase 1: Display Sleep (High Value, Medium Effort)
- [ ] Copy `display_sleep.c/h` pattern
- [ ] Adapt for ESP-IDF (no Arduino deps)
- [ ] Add panel handle and LVGL task handle to display init
- [ ] Call `display_activity_detected()` from all input paths
- [ ] Test dim → sleep → wake cycle
- [ ] Make timeouts configurable

### Phase 2: Encoder Abstraction (Medium Value, Low Effort)
- [ ] Add `encoder_get_value()` / `encoder_set_value()` API
- [ ] Add value change callback
- [ ] Update Roon volume control to use new API
- [ ] Sync encoder value with Roon API updates

### Phase 3: Context-Sensitive Input (Low Value, Low Effort)
- [ ] Add `encoder_activate()` / `encoder_deactivate()`
- [ ] Disable encoder on zone selector screen
- [ ] Enable encoder on now-playing screen

### Phase 4: Thread Safety (Medium Value, Low Effort)
- [ ] Add display state mutex
- [ ] Protect all display state changes
- [ ] Review LVGL thread safety

---

## 8. Code Snippets for Integration

### Display Sleep Init (add to platform_display_idf.c)

```c
// At top of file
#include "display_sleep.h"

// After LVGL task created:
TaskHandle_t lvgl_task_handle = /* get from lvgl_port_init */;
display_sleep_init(s_panel_handle, lvgl_task_handle);
ESP_LOGI(TAG, "Display sleep initialized (dim: 15s, sleep: 30s)");
```

### Touch Activity Detection (in LVGL touch callback)

```c
// platform_display_idf.c - lvgl_touch_read_cb
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x, y;

    if (tpGetCoordinates(&x, &y)) {
        display_activity_detected();  // ADD THIS
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
```

### Encoder Activity Detection (in encoder callback)

```c
// platform_input_idf.c - encoder_read_and_dispatch
static void encoder_read_and_dispatch(void) {
    // ... existing code ...

    int delta = s_encoder.count_value - last_count;
    if (delta != 0) {
        display_activity_detected();  // ADD THIS

        // ... rest of encoder handling ...
    }
}
```

---

## 9. Files to Reference

**Display Sleep:**
- `/tmp/smart-knob/display_sleep.h`
- `/tmp/smart-knob/display_sleep.c`

**Encoder Abstraction:**
- `/tmp/smart-knob/knob.h`
- `/tmp/smart-knob/knob.c`

**Bidirectional Switch (low-level):**
- `/tmp/smart-knob/bidi_switch_knob.h`
- `/tmp/smart-knob/bidi_switch_knob.c`

**Main Application:**
- `/tmp/smart-knob/smart_knob.ino`

---

## 10. Key Takeaways

1. **Display sleep is essential** for battery operation - most valuable feature
2. **Two-stage timeout** (dim then sleep) provides good UX
3. **LVGL task priority** lowering saves CPU during sleep
4. **Activity detection everywhere** - touch, encoder, buttons
5. **Thread safety matters** - use mutexes for shared state
6. **Value abstraction** better than raw events for encoder
7. **Context-sensitive input** useful for multi-screen apps
8. **Arduino code shows patterns** but ESP-IDF implementation is superior

---

## 11. Comparison with BlueKnob

| Feature | Smart Knob | BlueKnob | Roon Knob Current |
|---------|------------|----------|-------------------|
| **Framework** | Arduino | ESP-IDF | ESP-IDF ✓ |
| **I2C API** | Old (driver/i2c.h) | New (i2c_master) ✓ | New ✓ |
| **Display Sleep** | Yes ✓ | Partial | None |
| **Encoder Abstraction** | Yes ✓ | Basic | Basic |
| **Context-Sensitive** | Yes ✓ | No | No |
| **Thread Safety** | Mutex ✓ | Basic | Basic |
| **Battery Focus** | Medium | High ✓ | Low |
| **UI Framework** | LVGL + Squareline | LVGL + Squareline | LVGL manual |

**Best of both:**
- BlueKnob: I2C drivers, battery monitoring, power management philosophy
- Smart Knob: Display sleep implementation, encoder abstraction

**Roon Knob should:**
- Keep BlueKnob's I2C/touch drivers ✓ (already done)
- Add Smart Knob's display sleep pattern
- Adapt Smart Knob's encoder value API
- Keep ESP-IDF framework ✓

---

## 12. Estimated Implementation Time

| Feature | Effort | Value | Priority |
|---------|--------|-------|----------|
| Display Sleep | 2-3 hours | High | 1 |
| Encoder Abstraction | 1 hour | Medium | 2 |
| Context-Sensitive Input | 30 min | Low | 3 |
| Thread Safety Audit | 1 hour | Medium | 4 |
| **Total** | **4-5 hours** | | |

**ROI:** Display sleep alone is worth the effort for battery operation.
