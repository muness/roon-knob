// platform_input.h implementation for PhotoPainter buttons
// BOOT (GPIO0): short=prev track, long=WiFi setup AP mode
// GP4  (GPIO4): short=play/pause
// PWR  (GPIO5): short=next track, long=deep sleep

#include "platform/platform_input.h"
#include "ui.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static const char *TAG = "input";

#define BOOT_PIN  0
#define GP4_PIN   4
#define PWR_PIN   5

#define LONG_PRESS_MS 1000
#define DEBOUNCE_MS   50

static ui_input_cb_t s_input_cb = NULL;
static QueueHandle_t s_event_queue = NULL;

// Button state tracking
typedef struct {
    gpio_num_t pin;
    uint8_t active_low;  // 1 = active low (pressed = 0), 0 = active high
    uint64_t press_time;
    bool pressed;
} button_state_t;

static button_state_t s_boot = {BOOT_PIN, 1, 0, false};
static button_state_t s_gp4  = {GP4_PIN,  1, 0, false};
static button_state_t s_pwr  = {PWR_PIN,  0, 0, false};  // PWR is active high

static bool is_pressed(button_state_t *btn) {
    int level = gpio_get_level(btn->pin);
    return btn->active_low ? (level == 0) : (level == 1);
}

static void poll_button(button_state_t *btn, ui_input_event_t short_evt,
                        ui_input_event_t long_evt) {
    bool now_pressed = is_pressed(btn);
    uint64_t now = esp_timer_get_time() / 1000;  // ms

    if (now_pressed && !btn->pressed) {
        // Button just pressed
        btn->pressed = true;
        btn->press_time = now;
    } else if (!now_pressed && btn->pressed) {
        // Button released
        btn->pressed = false;
        uint64_t held = now - btn->press_time;
        if (held >= LONG_PRESS_MS && long_evt != UI_INPUT_NONE) {
            if (s_event_queue) xQueueSend(s_event_queue, &long_evt, 0);
        } else if (held >= DEBOUNCE_MS && short_evt != UI_INPUT_NONE) {
            if (s_event_queue) xQueueSend(s_event_queue, &short_evt, 0);
        }
    }
}

static void button_poll_cb(void *arg) {
    (void)arg;
    poll_button(&s_boot, UI_INPUT_PREV_TRACK, UI_INPUT_MENU);
    poll_button(&s_gp4,  UI_INPUT_PLAY_PAUSE, UI_INPUT_NONE);
    poll_button(&s_pwr,  UI_INPUT_NEXT_TRACK, UI_INPUT_NONE);
}

void platform_input_init(void) {
    s_event_queue = xQueueCreate(8, sizeof(ui_input_event_t));

    // Configure button GPIOs as inputs with pull-up
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOOT_PIN) | (1ULL << GP4_PIN) | (1ULL << PWR_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    // Timer to poll buttons every 10ms
    esp_timer_create_args_t timer_args = {
        .callback = button_poll_cb,
        .name = "btn_poll",
    };
    esp_timer_handle_t timer = NULL;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_periodic(timer, 10 * 1000);  // 10ms in microseconds

    ESP_LOGI(TAG, "Button input initialized (BOOT=%d, GP4=%d, PWR=%d)",
             BOOT_PIN, GP4_PIN, PWR_PIN);
}

void platform_input_process_events(void) {
    ui_input_event_t evt;
    while (s_event_queue && xQueueReceive(s_event_queue, &evt, 0) == pdTRUE) {
        ESP_LOGI(TAG, "Input event: %d", evt);
        if (s_input_cb) {
            s_input_cb(evt);
        }
    }
}

void platform_input_shutdown(void) {
    // Nothing to clean up
}

// Called from app_main.c via ui_set_input_handler / eink_ui equivalent
void platform_input_set_handler(ui_input_cb_t cb) {
    s_input_cb = cb;
}
