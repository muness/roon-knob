#include "platform/platform_input.h"
#include "platform_input_rlcd.h"

#include "controller_button_gesture.h"
#include "controller_input.h"
#include "controller_input_profile.h"
#include "rlcd_ui.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

#define RLCD_BOOT_PIN GPIO_NUM_0
#define RLCD_KEY_PIN GPIO_NUM_18
#define BUTTON_POLL_US 10000
#define DOUBLE_CLICK_MS 280
#define LONG_PRESS_MS 850

typedef struct {
    gpio_num_t pin;
    bool down;
    bool long_sent;
    uint32_t down_since_ms;
} button_t;
static button_t s_boot = {.pin = RLCD_BOOT_PIN};
static button_t s_key = {.pin = RLCD_KEY_PIN};
static controller_button_gesture_t s_boot_gesture;
static controller_button_gesture_t s_key_gesture;
static volatile bool s_boot_double;
static volatile bool s_boot_single;
static volatile bool s_key_double;
static volatile bool s_key_single;
static volatile bool s_boot_long;
static volatile bool s_key_long;
static bool s_key_track_mode;
static esp_timer_handle_t s_timer;
static volatile uint32_t s_last_activity_ms;
static const char *TAG = "rlcd_input";

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static bool pressed(button_t *button) { return gpio_get_level(button->pin) == 0; }

static void poll(void *arg) {
    (void)arg;
    uint32_t now = now_ms();
    bool boot_now = pressed(&s_boot);
    if (boot_now && !s_boot.down) {
        s_last_activity_ms = now;
        s_boot.down_since_ms = now;
        s_boot.long_sent = false;
    }
    if (boot_now && !s_boot.long_sent &&
        (uint32_t)(now - s_boot.down_since_ms) >= LONG_PRESS_MS) {
        s_boot.long_sent = true;
        s_boot_long = true;
        controller_button_gesture_reset(&s_boot_gesture);
    }
    if (!boot_now && s_boot.down) {
        if (!s_boot.long_sent) {
            controller_physical_gesture_t gesture = controller_button_gesture_on_release(
                &s_boot_gesture, now, DOUBLE_CLICK_MS);
            if (gesture == CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP) s_boot_double = true;
        }
        s_boot.long_sent = false;
    }
    s_boot.down = boot_now;
    if (controller_button_gesture_take_due(&s_boot_gesture, now) ==
        CONTROLLER_PHYSICAL_GESTURE_TAP) s_boot_single = true;
    bool key_now = pressed(&s_key);
    if (key_now && !s_key.down) {
        s_last_activity_ms = now;
        s_key.down_since_ms = now;
        s_key.long_sent = false;
    }
    if (key_now && !s_key.long_sent &&
        (uint32_t)(now - s_key.down_since_ms) >= LONG_PRESS_MS) {
        s_key.long_sent = true;
        s_key_long = true;
        controller_button_gesture_reset(&s_key_gesture);
    }
    if (!key_now && s_key.down) {
        if (!s_key.long_sent) {
            controller_physical_gesture_t gesture = controller_button_gesture_on_release(
                &s_key_gesture, now, DOUBLE_CLICK_MS);
            if (gesture == CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP) s_key_double = true;
        }
        s_key.long_sent = false;
    }
    s_key.down = key_now;
    if (controller_button_gesture_take_due(&s_key_gesture, now) ==
        CONTROLLER_PHYSICAL_GESTURE_TAP) s_key_single = true;
}

static void dispatch(uint16_t control, controller_physical_gesture_t gesture) {
    controller_physical_event_t event = {
        .source_id = CONTROLLER_INPUT_SOURCE_RLCD_BUTTONS, .control_id = control,
        .kind = CONTROLLER_PHYSICAL_EVENT_BUTTON, .gesture = gesture, .value = 1,
    };
    controller_action_t action;
    bool mapped = controller_input_resolve_physical(
        &event, controller_input_get_context(), &action);
    bool handled = controller_input_dispatch_physical(&event);
    ESP_LOGI(TAG, "%s %s -> %s%s", control == CONTROLLER_INPUT_CONTROL_RLCD_BOOT ? "BOOT" : "KEY",
             gesture == CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP ? "double" : "tap",
             mapped ? "mapped action" : "unmapped",
             mapped && !handled ? " (bridge unavailable)" : "");
}

void platform_input_init(void) {
    gpio_config_t config = {.mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << RLCD_BOOT_PIN) | (1ULL << RLCD_KEY_PIN),
        .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&config);
    s_last_activity_ms = now_ms();
    controller_button_gesture_reset(&s_boot_gesture);
    controller_button_gesture_reset(&s_key_gesture);
    esp_timer_create_args_t args = {.callback = poll, .name = "rlcd_buttons"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, BUTTON_POLL_US));
}
uint64_t rlcd_input_last_activity_ms(void) { return s_last_activity_ms; }
bool rlcd_input_wake_button_released(void) {
    /* KEY is the qualified Deep-sleep wake input. BOOT is also RTC-capable,
     * but using the ESP32-S3 strapping pin as a wake source could leave a
     * held button selecting the ROM downloader during the wake reset. */
    return gpio_get_level(RLCD_KEY_PIN) != 0;
}
void platform_input_process_events(void) {
    if (s_boot_long) {
        s_boot_long = false;
        if (!rlcd_ui_handle_activity()) {
            (void)rlcd_ui_enter_art_mode();
        }
    }
    if (s_key_long) {
        s_key_long = false;
        if (!rlcd_ui_handle_activity() &&
            controller_input_get_context() == CONTROLLER_INTERACTION_CONTEXT_MEDIA) {
            s_key_track_mode = !s_key_track_mode;
            controller_input_profile_rlcd_set_key_mode(s_key_track_mode);
            rlcd_ui_set_key_track_mode(s_key_track_mode);
        }
    }
    if (s_boot_double) {
        s_boot_double = false;
        if (!rlcd_ui_handle_activity()) {
            dispatch(CONTROLLER_INPUT_CONTROL_RLCD_BOOT, CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP);
        }
    }
    if (s_boot_single) {
        s_boot_single = false;
        if (!rlcd_ui_handle_activity()) {
            dispatch(CONTROLLER_INPUT_CONTROL_RLCD_BOOT, CONTROLLER_PHYSICAL_GESTURE_TAP);
        }
    }
    if (s_key_double) {
        s_key_double = false;
        if (!rlcd_ui_dismiss_usage_key() && !rlcd_ui_handle_activity()) {
            dispatch(CONTROLLER_INPUT_CONTROL_RLCD_KEY, CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP);
        }
    }
    if (s_key_single) {
        s_key_single = false;
        if (!rlcd_ui_dismiss_usage_key() && !rlcd_ui_handle_activity()) {
            dispatch(CONTROLLER_INPUT_CONTROL_RLCD_KEY,
                     CONTROLLER_PHYSICAL_GESTURE_TAP);
        }
    }
}
void platform_input_shutdown(void) {
    if (s_timer) { esp_timer_stop(s_timer); esp_timer_delete(s_timer); s_timer = NULL; }
}
