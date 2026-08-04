// platform_input.h implementation for PhotoPainter buttons
// BOOT (GPIO0): long press=WiFi setup AP mode (only action)
// GP4  (GPIO4): long press=restart
// PWR  (GPIO5): unused

#include "platform/platform_input.h"
#include "controller_input.h"
#include "controller_input_mailbox.h"
#include "controller_system_frame.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "input";

#define BOOT_PIN  0
#define GP4_PIN   4
#define PWR_PIN   5

#define LONG_PRESS_MS 1000
static esp_timer_handle_t s_btn_poll_timer = NULL;
static portMUX_TYPE s_latch_lock = portMUX_INITIALIZER_UNLOCKED;
static controller_input_safety_latch_t s_safety_latch;

#define FRAME_PENDING_PROVISIONING (1u << 0)
#define FRAME_PENDING_RESTART      (1u << 1)

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

static void offer_safety_action(uint32_t bit) {
    taskENTER_CRITICAL(&s_latch_lock);
    (void)controller_input_safety_latch_offer(&s_safety_latch, bit);
    taskEXIT_CRITICAL(&s_latch_lock);
}

static bool take_safety_action(uint32_t bit) {
    taskENTER_CRITICAL(&s_latch_lock);
    bool pending =
        controller_input_safety_latch_take(&s_safety_latch, bit);
    taskEXIT_CRITICAL(&s_latch_lock);
    return pending;
}

static void retry_safety_action(uint32_t bit) {
    taskENTER_CRITICAL(&s_latch_lock);
    controller_input_safety_latch_retry(&s_safety_latch, bit);
    taskEXIT_CRITICAL(&s_latch_lock);
}

static void poll_button(button_state_t *btn, uint32_t long_press_bit) {
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
        if (held >= LONG_PRESS_MS && long_press_bit != 0) {
            /*
             * Idempotent safety latches cannot overflow. A failed controller
             * dispatch is re-armed by the UI actor and retried.
             */
            offer_safety_action(long_press_bit);
        }
    }
}

// Track GP4 long-press for restart (handled outside the event queue)
static volatile bool s_restart_pending = false;

static void button_poll_cb(void *arg) {
    (void)arg;
    // BOOT: long press only = WiFi AP setup
    poll_button(&s_boot, FRAME_PENDING_PROVISIONING);
    // GP4/PWR: poll_button updates internal press state (press_time, pressed)
    // needed by the long-press restart check below, even though no events are enqueued
    poll_button(&s_gp4, 0);
    poll_button(&s_pwr, 0);

    // Check GP4 for restart (long press detection directly)
    bool gp4_pressed = is_pressed(&s_gp4);
    if (gp4_pressed && s_gp4.pressed) {
        uint64_t held = (esp_timer_get_time() / 1000) - s_gp4.press_time;
        if (held >= LONG_PRESS_MS && !s_restart_pending) {
            s_restart_pending = true;
            offer_safety_action(FRAME_PENDING_RESTART);
        }
    }
}

void platform_input_init(void) {
    taskENTER_CRITICAL(&s_latch_lock);
    controller_input_safety_latch_reset(&s_safety_latch);
    taskEXIT_CRITICAL(&s_latch_lock);
    s_restart_pending = false;
    s_boot = (button_state_t){BOOT_PIN, 1, 0, false};
    s_gp4 = (button_state_t){GP4_PIN, 1, 0, false};
    s_pwr = (button_state_t){PWR_PIN, 0, 0, false};
    controller_system_frame_init();

    // Active-low buttons (BOOT, GP4): pull-up, read 0 when pressed
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOOT_PIN) | (1ULL << GP4_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    // Active-high button (PWR): pull-down, read 1 when pressed
    gpio_config_t pwr_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PWR_PIN),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&pwr_conf);

    // Timer to poll buttons every 10ms
    esp_timer_create_args_t timer_args = {
        .callback = button_poll_cb,
        .name = "btn_poll",
    };
    if (esp_timer_create(&timer_args, &s_btn_poll_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button poll timer");
        return;
    }
    if (esp_timer_start_periodic(s_btn_poll_timer, 10 * 1000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start button poll timer");
        esp_timer_delete(s_btn_poll_timer);
        s_btn_poll_timer = NULL;
        return;
    }

    ESP_LOGI(TAG, "Button input initialized (BOOT=%d, GP4=%d, PWR=%d)",
             BOOT_PIN, GP4_PIN, PWR_PIN);
}

void platform_input_process_events(void) {
    if (take_safety_action(FRAME_PENDING_PROVISIONING)) {
        controller_physical_event_t event = {
            .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
            .control_id = CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
            .kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
            .gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD,
            .value = 1,
            .sequence = 0,
            .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
        };
        if (!controller_system_frame_dispatch_physical(&event)) {
            retry_safety_action(FRAME_PENDING_PROVISIONING);
        }
    }
    if (take_safety_action(FRAME_PENDING_RESTART)) {
        controller_physical_event_t event = {
            .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
            .control_id = CONTROLLER_INPUT_CONTROL_FRAME_GP4,
            .kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
            .gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD,
            .value = 1,
            .sequence = 0,
            .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
        };
        if (!controller_system_frame_dispatch_physical(&event)) {
            retry_safety_action(FRAME_PENDING_RESTART);
        }
    }
}

controller_input_mailbox_stats_t platform_input_mailbox_stats(void) {
    taskENTER_CRITICAL(&s_latch_lock);
    controller_input_mailbox_stats_t stats = s_safety_latch.stats;
    taskEXIT_CRITICAL(&s_latch_lock);
    return stats;
}

void platform_input_shutdown(void) {
    if (s_btn_poll_timer) {
        esp_timer_stop(s_btn_poll_timer);
        esp_timer_delete(s_btn_poll_timer);
        s_btn_poll_timer = NULL;
    }
    controller_system_frame_shutdown();
    taskENTER_CRITICAL(&s_latch_lock);
    controller_input_safety_latch_reset(&s_safety_latch);
    taskEXIT_CRITICAL(&s_latch_lock);
}
