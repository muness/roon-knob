# FreeRTOS Patterns

This document covers the FreeRTOS patterns used in the Roon Knob firmware: tasks, queues, semaphores, and timers.

## Overview

The firmware uses FreeRTOS (via ESP-IDF) for:
- **Tasks** - Concurrent execution threads
- **Queues** - Thread-safe message passing (ISR → task)
- **Semaphores** - Mutual exclusion for shared state
- **Timers** - Periodic and one-shot callbacks

## Tasks

### Task Summary

| Task | Stack | Priority | Purpose |
|------|-------|----------|---------|
| `ui_loop` | 8KB | 2 | LVGL rendering, input processing |
| `ota_check` | 8KB | 1 | Check for firmware updates |
| `ota_update` | 8KB | 1 | Download and flash firmware |
| `dns_server` | 4KB | 5 | DNS hijacking for captive portal |

### Priority Levels

```
Priority 5: dns_server      (highest - network critical)
Priority 2: ui_loop         (responsive UI)
Priority 1: ota_check/update (background, non-blocking)
Priority 0: IDLE            (lowest - system task)
```

Higher priority tasks preempt lower priority ones. The UI runs at priority 2 to stay responsive while allowing network tasks to preempt when needed.

### Creating Tasks

```c
TaskHandle_t task_handle = NULL;

xTaskCreate(
    task_function,      // Function pointer
    "task_name",        // Name (for debugging)
    8192,               // Stack size in bytes
    NULL,               // Parameter passed to function
    2,                  // Priority (0-24, higher = more important)
    &task_handle        // Handle for later control
);
```

### Stack Size Guidelines

| Stack Size | Use Case |
|------------|----------|
| 4KB | Simple tasks, no deep call stacks |
| 8KB | LVGL operations, HTTP client, complex processing |
| 16KB+ | Rarely needed, check for stack overflow first |

The UI loop uses 8KB because LVGL theme initialization has deep call stacks.

### Self-Deleting Tasks

For one-shot operations like OTA:

```c
static void check_update_task(void *arg) {
    // ... do work ...

    s_ota_task = NULL;  // Clear handle before delete
    vTaskDelete(NULL);  // Delete self (NULL = current task)
}
```

## Queues

### Input Event Queue

The rotary encoder runs in timer/ISR context and sends events to the UI task via a queue:

```c
// Create queue (in platform_input_init)
static QueueHandle_t s_input_queue = NULL;
s_input_queue = xQueueCreate(10, sizeof(ui_input_event_t));

// Send from ISR/timer context
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
xQueueSendFromISR(s_input_queue, &input, &xHigherPriorityTaskWoken);
if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();  // Immediately switch to higher priority task
}

// Receive in task context (non-blocking)
ui_input_event_t input;
while (xQueueReceive(s_input_queue, &input, 0) == pdTRUE) {
    ui_dispatch_input(input);
}
```

### Queue Patterns

**ISR → Task (non-blocking send):**
```c
xQueueSendFromISR(queue, &data, &woken);
```

**Task → Task (blocking send):**
```c
xQueueSend(queue, &data, pdMS_TO_TICKS(100));  // Wait up to 100ms
```

**Non-blocking receive (polling):**
```c
if (xQueueReceive(queue, &data, 0) == pdTRUE) {
    // Got data
}
```

**Blocking receive (wait forever):**
```c
xQueueReceive(queue, &data, portMAX_DELAY);
```

## Semaphores (Mutexes)

### Display State Mutex

Protects display state from concurrent access by timer callbacks and UI task:

```c
static SemaphoreHandle_t s_display_state_mutex = NULL;

// Create mutex (in init)
s_display_state_mutex = xSemaphoreCreateMutex();

// Lock/unlock macros
#define LOCK_DISPLAY_STATE() xSemaphoreTake(s_display_state_mutex, portMAX_DELAY)
#define UNLOCK_DISPLAY_STATE() xSemaphoreGive(s_display_state_mutex)

// Usage
void display_dim(void) {
    LOCK_DISPLAY_STATE();
    if (s_display_state == DISPLAY_STATE_NORMAL) {
        // Safe to modify state
        s_display_state = DISPLAY_STATE_DIM;
    }
    UNLOCK_DISPLAY_STATE();
}
```

### Mutex Guidelines

- Always pair `Take` with `Give`
- Use `portMAX_DELAY` for indefinite wait (or timeout for deadlock detection)
- Keep critical sections short
- Never call blocking functions while holding mutex

## Timers

ESP-IDF provides `esp_timer` which runs callbacks in a dedicated high-priority task.

### Timer Types

| Timer | Type | Interval | Purpose |
|-------|------|----------|---------|
| `lvgl_tick` | Periodic | 2ms | LVGL time tracking |
| `input_poll` | Periodic | 3ms | Rotary encoder polling |
| `wifi_retry` | One-shot | Variable | Exponential backoff |
| `display_dim` | One-shot | 30s | Dim timeout |
| `display_sleep` | One-shot | 60s | Sleep timeout |

### Creating Timers

**Periodic timer:**
```c
static esp_timer_handle_t s_timer = NULL;

const esp_timer_create_args_t timer_args = {
    .callback = timer_callback,
    .name = "my_timer"
};
esp_timer_create(&timer_args, &s_timer);
esp_timer_start_periodic(s_timer, 3000);  // 3000 microseconds = 3ms
```

**One-shot timer:**
```c
esp_timer_start_once(s_timer, 30000000);  // 30 seconds in microseconds
```

### Restarting One-Shot Timers

For activity-based timeouts:

```c
void display_activity_detected(void) {
    esp_timer_stop(s_dim_timer);    // Stop current timer
    esp_timer_start_once(s_dim_timer, 30000000);  // Restart
}
```

### Timer Callback Constraints

Timer callbacks run in `esp_timer` task context (~3KB stack). Avoid:
- Heavy computation
- Blocking operations
- Large stack allocations

Use the deferred operation pattern for heavy work:

```c
static volatile bool s_heavy_work_pending = false;

// In timer callback (limited stack)
static void timer_callback(void *arg) {
    s_heavy_work_pending = true;  // Just set flag
}

// In main task (8KB stack)
if (s_heavy_work_pending) {
    s_heavy_work_pending = false;
    do_heavy_work();  // Safe here
}
```

## ISR → Task Pattern

The encoder demonstrates the full pattern for ISR-safe input handling:

```
┌─────────────────────────────────────────────────────────────────┐
│ Timer ISR (esp_timer context, ~3KB stack)                       │
│                                                                 │
│   1. Read GPIO levels                                           │
│   2. Decode quadrature                                          │
│   3. xQueueSendFromISR(queue, &event)                           │
│   4. portYIELD_FROM_ISR if higher priority task woken           │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                            Queue (10 items)
                                 │
┌────────────────────────────────▼────────────────────────────────┐
│ UI Task (FreeRTOS task, 8KB stack)                              │
│                                                                 │
│   while (xQueueReceive(queue, &event, 0)) {                     │
│       display_activity_detected();  // Wake display             │
│       ui_dispatch_input(event);     // Handle input             │
│   }                                                             │
└─────────────────────────────────────────────────────────────────┘
```

## Common Patterns

### Task Synchronization

**Wait for initialization:**
```c
static volatile bool s_initialized = false;

void init_task(void *arg) {
    // ... initialization ...
    s_initialized = true;
    vTaskDelete(NULL);
}

// In another task
while (!s_initialized) {
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

**Event groups (alternative):**
```c
static EventGroupHandle_t s_events;
#define INIT_DONE_BIT BIT0

// Signal
xEventGroupSetBits(s_events, INIT_DONE_BIT);

// Wait
xEventGroupWaitBits(s_events, INIT_DONE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
```

### Periodic Work in Task

```c
static void worker_task(void *arg) {
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // Do periodic work
        process_data();

        // Sleep until next period (more accurate than vTaskDelay)
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}
```

### Clean Shutdown

```c
static volatile bool s_running = true;

static void my_task(void *arg) {
    while (s_running) {
        // ... work ...
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

void shutdown(void) {
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for task to exit
}
```

## Debugging

### Stack Overflow Detection

Enable in menuconfig:
```
Component config → FreeRTOS → Enable stack overflow detection
```

### Task Stats

```c
char stats[512];
vTaskList(stats);
ESP_LOGI(TAG, "Tasks:\n%s", stats);
```

### High Water Mark

Check how much stack was used:
```c
UBaseType_t high_water = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "Stack remaining: %u bytes", high_water * sizeof(StackType_t));
```

## Implementation Files

| File | FreeRTOS Usage |
|------|----------------|
| `main_idf.c` | UI task creation |
| `platform_input_idf.c` | Input queue, poll timer |
| `display_sleep.c` | State mutex, dim/sleep timers |
| `platform_display_idf.c` | LVGL tick timer |
| `wifi_manager.c` | Retry timer |
| `dns_server.c` | DNS server task |
| `ota_update.c` | OTA tasks |
