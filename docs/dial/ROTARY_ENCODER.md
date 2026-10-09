# Rotary Encoder Input

This document covers how the firmware reads the physical knob (rotary encoder) and translates rotation into volume control.

## Hardware Overview

| Component | Type | Interface |
|-----------|------|-----------|
| Encoder | Incremental quadrature | GPIO |
| Channel A | GPIO 8 | Input with pull-up |
| Channel B | GPIO 7 | Input with pull-up |

The rotary encoder is a mechanical component that outputs two square wave signals (A and B) as the knob rotates. The phase relationship between these signals indicates rotation direction.

## Quadrature Encoding

A rotary encoder produces two signals, 90° out of phase:

```
Clockwise rotation:
Channel A:  ──┐   ┌───┐   ┌───
              └───┘   └───┘
Channel B:  ────┐   ┌───┐   ┌─
                └───┘   └───┘

Counter-clockwise:
Channel A:  ────┐   ┌───┐   ┌─
                └───┘   └───┘
Channel B:  ──┐   ┌───┐   ┌───
              └───┘   └───┘
```

The firmware detects direction by observing which channel transitions first:
- **A rises before B** → clockwise (volume up)
- **B rises before A** → counter-clockwise (volume down)

## Software Decoding

Rather than using hardware encoder peripherals, the firmware polls both GPIOs and decodes in software. This approach is simple and works reliably at the poll rate used.

### GPIO Configuration

```c
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << ENCODER_GPIO_A),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,    // Internal pull-up
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,      // No interrupts, we poll
};
gpio_config(&io_conf);
```

Both channels use internal pull-ups. The encoder's mechanical contacts connect the pin to ground when closed, so:
- **High (1)** = contact open
- **Low (0)** = contact closed

### Debouncing

Mechanical encoders produce contact bounce - rapid on/off transitions when the contacts are closing or opening. The firmware uses a simple debounce counter:

```c
typedef struct {
    uint8_t debounce_a_cnt;
    uint8_t debounce_b_cnt;
    uint8_t encoder_a_level;   // Last stable level
    uint8_t encoder_b_level;
    int count_value;           // Accumulated rotation count
} encoder_state_t;

#define ENCODER_DEBOUNCE_TICKS 2  // Must see same level 2x
```

A level change is only accepted after seeing the same value for `ENCODER_DEBOUNCE_TICKS` consecutive polls.

### Decoding Algorithm

The decoder processes each channel independently:

```c
static void process_encoder_channel(uint8_t current_level, uint8_t *prev_level,
                                    uint8_t *debounce_cnt, int *count_value,
                                    bool is_increment) {
    if (current_level == 0) {
        // Going low - reset debounce
        if (current_level != *prev_level) {
            *debounce_cnt = 0;
        } else {
            (*debounce_cnt)++;
        }
    } else {
        // Going high - count on stable transition
        if (current_level != *prev_level && ++(*debounce_cnt) >= ENCODER_DEBOUNCE_TICKS) {
            *debounce_cnt = 0;
            *count_value += is_increment ? 1 : -1;
        } else {
            *debounce_cnt = 0;
        }
    }
    *prev_level = current_level;
}
```

Channel A increments the count, Channel B decrements. The net result gives rotation direction:
- **Clockwise**: A transitions before B → count increases
- **Counter-clockwise**: B transitions before A → count decreases

## Polling

A periodic timer polls the encoder every 3ms:

```c
#define ENCODER_POLL_INTERVAL_MS 3

static void input_poll_timer_callback(void* arg) {
    encoder_read_and_dispatch();
}

esp_timer_start_periodic(s_poll_timer, ENCODER_POLL_INTERVAL_MS * 1000);
```

The 3ms interval is fast enough to catch encoder transitions at reasonable rotation speeds (~300 RPM max).

## Event Dispatch

When the count changes, an event is queued for the UI:

```c
static void encoder_read_and_dispatch(void) {
    // ... polling and decoding ...

    int delta = s_encoder.count_value - last_count;
    if (delta != 0) {
        last_count = s_encoder.count_value;

        ui_input_event_t input = (delta > 0) ? UI_INPUT_VOL_UP : UI_INPUT_VOL_DOWN;

        // Queue from timer context (ISR-safe)
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(s_input_queue, &input, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}
```

The timer callback runs in interrupt context, so `xQueueSendFromISR` is used instead of `xQueueSend`.

## Event Queue

Input events flow through a FreeRTOS queue:

```c
static QueueHandle_t s_input_queue = NULL;

// At init:
s_input_queue = xQueueCreate(10, sizeof(ui_input_event_t));

// In main loop:
void platform_input_process_events(void) {
    ui_input_event_t input;
    while (xQueueReceive(s_input_queue, &input, 0) == pdTRUE) {
        display_activity_detected();  // Wake display
        ui_dispatch_input(input);     // Send to UI layer
    }
}
```

This decouples the ISR-context encoder reading from the main-context UI handling.

## Input Event Types

```c
typedef enum {
    UI_INPUT_VOL_DOWN = -1,
    UI_INPUT_NONE = 0,
    UI_INPUT_VOL_UP = 1,
    UI_INPUT_PLAY_PAUSE = 2,  // Not used by encoder
    UI_INPUT_MENU = 3,        // Not used by encoder
    UI_INPUT_NEXT_TRACK = 4,  // Not used by encoder
    UI_INPUT_PREV_TRACK = 5,  // Not used by encoder
} ui_input_event_t;
```

The encoder only generates `UI_INPUT_VOL_UP` and `UI_INPUT_VOL_DOWN`. Other events come from touch input.

## Display Wake

Any encoder movement wakes the display from sleep:

```c
void platform_input_process_events(void) {
    ui_input_event_t input;
    while (xQueueReceive(s_input_queue, &input, 0) == pdTRUE) {
        display_activity_detected();  // Reset sleep timers
        ui_dispatch_input(input);
    }
}
```

## Acceleration

The current implementation sends one `VOL_UP` or `VOL_DOWN` per encoder detent. For finer control or acceleration (faster = bigger jumps), you could:

1. **Track velocity**: Measure time between events, increase delta for fast rotation
2. **Batch events**: Accumulate multiple ticks into larger volume changes
3. **Use encoder counts directly**: Pass the delta value rather than discrete up/down events

## Why Polling Instead of Interrupts?

Interrupts seem natural for encoders, but polling has advantages:

1. **Simpler debouncing** - ISR-based debounce requires timers or state machines
2. **No race conditions** - Polling from a single timer context is inherently thread-safe
3. **Predictable timing** - Fixed 3ms intervals, no interrupt storms from bounce
4. **CPU cost is negligible** - Reading two GPIOs and some arithmetic takes microseconds

The ESP32 PCNT (pulse counter) peripheral could hardware-decode the encoder, but software decoding is simpler and the poll overhead is minimal.

## Pin Mapping Summary

| Function | GPIO | Pull | Notes |
|----------|------|------|-------|
| Encoder A | 8 | Up | Quadrature channel A |
| Encoder B | 7 | Up | Quadrature channel B |

## Common Issues

**No response to rotation**: Check GPIO numbers match your hardware. Verify pull-ups are enabled.

**Direction is inverted**: Swap `UI_INPUT_VOL_UP` and `UI_INPUT_VOL_DOWN` in `encoder_read_and_dispatch()`, or swap the A/B GPIO definitions.

**Missed steps at high speed**: Decrease `ENCODER_POLL_INTERVAL_MS` (but below ~1ms may impact other tasks).

**Double-counting or erratic behavior**: Increase `ENCODER_DEBOUNCE_TICKS` or check for electrical noise on GPIO lines.
