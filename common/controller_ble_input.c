#include "controller_ble_input.h"

#include "controller_input.h"

#include "controller_shortcut_key.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <stdint.h>

static const char *TAG = "controller_ble";

void controller_ble_input_on_media_key(rk_ble_media_key_t key, void *context) {
    (void)context;

    controller_control_intent_t intent = CONTROLLER_CONTROL_INTENT_NONE;
    switch (key) {
    case RK_BLE_MEDIA_KEY_PLAY_PAUSE:
        intent = CONTROLLER_CONTROL_INTENT_ACTIVATE;
        break;
    case RK_BLE_MEDIA_KEY_NEXT_TRACK:
        intent = CONTROLLER_CONTROL_INTENT_NEXT;
        break;
    case RK_BLE_MEDIA_KEY_PREVIOUS_TRACK:
        intent = CONTROLLER_CONTROL_INTENT_PREVIOUS;
        break;
    case RK_BLE_MEDIA_KEY_VOLUME_UP:
        intent = CONTROLLER_CONTROL_INTENT_VOLUME_UP;
        break;
    case RK_BLE_MEDIA_KEY_VOLUME_DOWN:
        intent = CONTROLLER_CONTROL_INTENT_VOLUME_DOWN;
        break;
    case RK_BLE_MEDIA_KEY_NONE:
    default:
        return;
    }

    if (!controller_input_post_control(intent)) {
        ESP_LOGW(TAG, "Controller input queue full; BLE media key %d dropped",
                 key);
    } else {
        ESP_LOGI(TAG, "BLE media key %d queued as control intent %d", key,
                 intent);
    }
}

/*
 * Remote keys as bridge shortcuts.
 *
 * The recogniser (controller_shortcut_key.c) is single-threaded, so it lives on
 * the UI actor: BLE callbacks and the long-press timer only post to it. Its
 * timestamps are taken when the UI actor handles an event, a few milliseconds
 * after the radio saw it, which is far inside the 600 ms / 350 ms windows.
 *
 * The router accepts the action only in the media context, and the backend
 * adapter owns the request: it runs off the UI task, one at a time, and a press
 * that arrives while one is in flight is dropped rather than queued.
 */
static controller_shortcut_gesture_state_t s_gesture; /* UI actor only */
static esp_timer_handle_t s_timer;                    /* UI actor only */

static uint32_t now_ms(void) { return (uint32_t)platform_millis(); }

/* A recognised gesture is semantic input like any other: it enters the shared
 * action router, which decides by context whether to honour it and hands it to
 * the backend adapter. Nothing here knows the bridge or its transport. */
static void dispatch_shortcut(const controller_shortcut_event_t *event) {
    const controller_action_t action = controller_action_shortcut(
        event->usage, (uint8_t)event->gesture);
    if (!controller_input_dispatch_action(&action)) {
        ESP_LOGI(TAG, "Shortcut key %04x gesture %d not dispatched",
                 (unsigned)event->usage, (int)event->gesture);
    }
}

static void on_tick_ui(void *arg);

static void timer_fired(void *arg) {
    (void)arg;
    (void)platform_task_post_to_ui(on_tick_ui, NULL);
}

/* Re-arm a one-shot for the next long press or double window, or leave it off. */
static void arm_timer(void) {
    const uint32_t due =
        controller_shortcut_gesture_next_due_ms(&s_gesture, now_ms());
    if (!s_timer) {
        const esp_timer_create_args_t args = {
            .callback = timer_fired,
            .name = "shortcut_gesture",
        };
        if (esp_timer_create(&args, &s_timer) != ESP_OK) {
            s_timer = NULL;
            ESP_LOGW(TAG, "Could not create shortcut timer");
            return;
        }
    }
    (void)esp_timer_stop(s_timer); /* not running is fine */
    if (due != UINT32_MAX) {
        (void)esp_timer_start_once(s_timer, ((uint64_t)due + 1u) * 1000u);
    }
}

static void on_tick_ui(void *arg) {
    (void)arg;
    controller_shortcut_event_t event;
    for (int i = 0; i < 2; ++i) {
        if (controller_shortcut_gesture_poll(&s_gesture, now_ms(), &event) == 0) {
            break;
        }
        dispatch_shortcut(&event);
    }
    arm_timer();
}

static void on_key_ui(void *arg) {
    const uintptr_t packed = (uintptr_t)arg;
    const uint16_t usage = (uint16_t)(packed >> 1);
    const bool pressed = (packed & 1u) != 0;
    if (usage == 0) {
        /* The BLE session ended: a held or pending key must not outlive it. */
        controller_shortcut_gesture_reset(&s_gesture);
        arm_timer();
        return;
    }
    controller_shortcut_event_t events[2];
    const size_t count = controller_shortcut_gesture_on_key(
        &s_gesture, usage, pressed, now_ms(), events);
    for (size_t i = 0; i < count; ++i) {
        dispatch_shortcut(&events[i]);
    }
    arm_timer();
}

void controller_ble_input_on_consumer_event(uint16_t usage, bool pressed,
                                            void *context) {
    (void)context;
    const uintptr_t packed = ((uintptr_t)usage << 1) | (pressed ? 1u : 0u);
    if (!platform_task_post_to_ui(on_key_ui, (void *)packed)) {
        ESP_LOGW(TAG, "UI queue full; remote key %04x dropped", (unsigned)usage);
    }
}
