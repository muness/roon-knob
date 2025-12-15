#include "platform/platform_input.h"
#include "ui.h"
#include "display_sleep.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "input";

// Input event queue (for ISR-safe dispatch)
static QueueHandle_t s_input_queue = NULL;

// ============================================================================
// Hardware Pin Configuration
// ============================================================================
// These GPIO pins should be verified against your actual hardware schematic.
// Default values below assume common ESP32-S3 breakout board layout.

// Rotary encoder quadrature signals (hardware-specific pins)
#define ENCODER_GPIO_A    GPIO_NUM_8   // Encoder channel A (ECA)
#define ENCODER_GPIO_B    GPIO_NUM_7   // Encoder channel B (ECB)

// Touch screen - CST816 capacitive touch controller on I2C
// Note: This device has NO physical buttons - all interactions via touchscreen or encoder
// Touch input is handled by LVGL touch driver, not here
// See: lcd_config.h for touch I2C pins (GPIO 11=SDA, 12=SCL)

// ============================================================================
// Rotary Encoder Configuration
// ============================================================================
#define ENCODER_POLL_INTERVAL_MS 3      // Poll encoder every 3ms (matching hardware demo)
#define ENCODER_DEBOUNCE_TICKS   2      // Debounce count
#define ENCODER_BATCH_INTERVAL_MS 50    // Batch encoder ticks over 50ms window for velocity detection


// ============================================================================
// State Variables
// ============================================================================
static esp_timer_handle_t s_poll_timer = NULL;

// Software encoder state
typedef struct {
    uint8_t debounce_a_cnt;
    uint8_t debounce_b_cnt;
    uint8_t encoder_a_level;
    uint8_t encoder_b_level;
    int count_value;
} encoder_state_t;

static encoder_state_t s_encoder = {0};

// ============================================================================
// Rotary Encoder Implementation (Software Quadrature Decoding)
// ============================================================================

static esp_err_t encoder_init(void) {
    ESP_LOGI(TAG, "Initializing rotary encoder on GPIOs %d and %d", ENCODER_GPIO_A, ENCODER_GPIO_B);

    // Configure encoder A GPIO
    gpio_config_t io_conf_a = {
        .pin_bit_mask = (1ULL << ENCODER_GPIO_A),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf_a));

    // Configure encoder B GPIO
    gpio_config_t io_conf_b = {
        .pin_bit_mask = (1ULL << ENCODER_GPIO_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf_b));

    // Initialize encoder state
    s_encoder.encoder_a_level = gpio_get_level(ENCODER_GPIO_A);
    s_encoder.encoder_b_level = gpio_get_level(ENCODER_GPIO_B);
    s_encoder.count_value = 0;

    ESP_LOGI(TAG, "Rotary encoder initialized successfully");
    return ESP_OK;
}

// Software quadrature decoder (based on vendor demo code)
static void process_encoder_channel(uint8_t current_level, uint8_t *prev_level,
                                    uint8_t *debounce_cnt, int *count_value,
                                    bool is_increment) {
    if (current_level == 0) {
        if (current_level != *prev_level) {
            *debounce_cnt = 0;
        } else {
            (*debounce_cnt)++;
        }
    } else {
        if (current_level != *prev_level && ++(*debounce_cnt) >= ENCODER_DEBOUNCE_TICKS) {
            *debounce_cnt = 0;
            *count_value += is_increment ? 1 : -1;
        } else {
            *debounce_cnt = 0;
        }
    }
    *prev_level = current_level;
}

// Accumulated encoder ticks for velocity-sensitive batching
static int s_accumulated_ticks = 0;
static int64_t s_last_batch_time = 0;

static void encoder_read_and_dispatch(void) {
    static int last_count = 0;

    // Read current levels
    uint8_t pha_value = gpio_get_level(ENCODER_GPIO_A);
    uint8_t phb_value = gpio_get_level(ENCODER_GPIO_B);

    // Process both channels (software quadrature decoding)
    process_encoder_channel(pha_value, &s_encoder.encoder_a_level,
                           &s_encoder.debounce_a_cnt, &s_encoder.count_value, true);
    process_encoder_channel(phb_value, &s_encoder.encoder_b_level,
                           &s_encoder.debounce_b_cnt, &s_encoder.count_value, false);

    // Accumulate ticks (don't dispatch immediately)
    int delta = s_encoder.count_value - last_count;
    if (delta != 0) {
        s_accumulated_ticks += delta;
        last_count = s_encoder.count_value;
    }

    // Check if it's time to dispatch batched ticks
    int64_t now = esp_timer_get_time() / 1000;  // Convert to ms
    if (now - s_last_batch_time >= ENCODER_BATCH_INTERVAL_MS) {
        if (s_accumulated_ticks != 0) {
            ESP_LOGD(TAG, "Encoder batch: %d ticks over %lldms",
                     s_accumulated_ticks, now - s_last_batch_time);

            // Queue a volume rotation event with the accumulated ticks
            // We use a special sentinel value to pass the tick count through the queue
            // The sign indicates direction, magnitude indicates tick count
            int ticks_to_send = s_accumulated_ticks;
            s_accumulated_ticks = 0;

            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(s_input_queue, &ticks_to_send, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
        s_last_batch_time = now;
    }
}

// ============================================================================
// Touch Input (handled by LVGL touch driver)
// ============================================================================
// Touch input is registered separately via LVGL's CST816 touch driver
// Physical touch events will be handled by LVGL and can trigger UI callbacks
// For now, encoder is the only input method implemented here

// ============================================================================
// Polling Timer
// ============================================================================

static void input_poll_timer_callback(void* arg) {
    (void)arg;
    encoder_read_and_dispatch();
}

static esp_err_t poll_timer_init(void) {
    const esp_timer_create_args_t timer_args = {
        .callback = &input_poll_timer_callback,
        .name = "input_poll"
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_poll_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_poll_timer, ENCODER_POLL_INTERVAL_MS * 1000));  // Convert ms to us

    ESP_LOGI(TAG, "Input polling timer started (%d ms interval)", ENCODER_POLL_INTERVAL_MS);
    return ESP_OK;
}

// ============================================================================
// Public API
// ============================================================================

void platform_input_init(void) {
    ESP_LOGI(TAG, "Initializing platform input (encoder only - touch handled by LVGL)");

    // Create input event queue (holds up to 10 batched tick counts)
    s_input_queue = xQueueCreate(10, sizeof(int));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "Failed to create input event queue");
        return;
    }

    esp_err_t err;

    err = encoder_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize encoder: %s", esp_err_to_name(err));
        return;
    }

    err = poll_timer_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize poll timer: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Platform input initialized successfully (encoder polling at %dms)", ENCODER_POLL_INTERVAL_MS);
}

void platform_input_process_events(void) {
    int ticks;
    int total_ticks = 0;

    // Drain all queued batches and coalesce into single total
    // This prevents HTTP request queue buildup when turning quickly
    while (xQueueReceive(s_input_queue, &ticks, 0) == pdTRUE) {
        total_ticks += ticks;
    }

    if (total_ticks != 0) {
        display_activity_detected();  // Wake display and reset sleep timers
        // Dispatch single volume rotation with coalesced tick count
        ui_handle_volume_rotation(total_ticks);
    }
}

void platform_input_shutdown(void) {
    ESP_LOGI(TAG, "Shutting down platform input");

    if (s_poll_timer) {
        esp_timer_stop(s_poll_timer);
        esp_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }

    // Reset encoder GPIOs
    gpio_reset_pin(ENCODER_GPIO_A);
    gpio_reset_pin(ENCODER_GPIO_B);

    ESP_LOGI(TAG, "Platform input shutdown complete");
}
