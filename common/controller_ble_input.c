// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_ble_input.h"

#include "controller_input.h"

#include "controller_shortcut_key.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"

#include <esp_log.h>
#include "os_critical.h"
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

/* A bounded, timestamped FIFO between the HID owner and the UI actor. Only a
 * producer watermark can advance time. Queue loss invalidates the entire
 * uncertain gesture out of band, never by synthesizing a release. */
#define CONSUMER_MAILBOX_SIZE 32
static os_critical_t s_mailbox_lock = OS_CRITICAL_INITIALIZER;
static rk_ble_consumer_event_t s_mailbox[CONSUMER_MAILBOX_SIZE];
static unsigned s_head, s_count;
static uint32_t s_session, s_cancel_revision, s_seen_cancel, s_last_sequence;
static bool s_wake_queued, s_have_sequence, s_faulted;
static controller_ble_input_stats_t s_stats;
static controller_shortcut_gesture_state_t s_gesture; /* UI actor only */

static void dispatch_shortcut(const controller_shortcut_event_t *event, uint32_t revision) {
    os_critical_enter(&s_mailbox_lock);
    bool current = revision == s_cancel_revision;
    os_critical_leave(&s_mailbox_lock);
    if (!current) return;
    const controller_action_t action = controller_action_shortcut(event->usage, (uint8_t)event->gesture);
    (void)controller_input_dispatch_action(&action);
}

void controller_ble_input_service(void) {
    for (unsigned n = 0; n < CONSUMER_MAILBOX_SIZE; ++n) {
        os_critical_enter(&s_mailbox_lock);
        if (s_seen_cancel != s_cancel_revision) {
            controller_shortcut_gesture_reset(&s_gesture);
            s_seen_cancel = s_cancel_revision;
            s_have_sequence = false;
        }
        if (!s_count) { os_critical_leave(&s_mailbox_lock); break; }
        rk_ble_consumer_event_t edge = s_mailbox[s_head];
        s_head = (s_head + 1) % CONSUMER_MAILBOX_SIZE; --s_count;
        uint32_t revision = s_cancel_revision;
        os_critical_leave(&s_mailbox_lock);
        if (s_have_sequence && (int32_t)(edge.sequence - s_last_sequence) <= 0) {
            controller_shortcut_gesture_reset(&s_gesture);
            continue;
        }
        s_last_sequence = edge.sequence; s_have_sequence = true;
        controller_shortcut_event_t events[2];
        for (unsigned i = 0; i < 2; ++i) {
            if (!controller_shortcut_gesture_poll(&s_gesture, edge.time_ms, &events[0])) break;
            dispatch_shortcut(&events[0], revision);
        }
        if (edge.kind == RK_BLE_CONSUMER_EDGE) {
            size_t count = controller_shortcut_gesture_on_key(&s_gesture,
                edge.usage, edge.pressed, edge.time_ms, events);
            for (size_t i=0; i<count; ++i) dispatch_shortcut(&events[i], revision);
        }
    }
}
static void drain_on_ui(void *arg) {
    (void)arg;
    os_critical_enter(&s_mailbox_lock); s_wake_queued = false; os_critical_leave(&s_mailbox_lock);
    controller_ble_input_service();
}
void controller_ble_input_on_consumer_event(const rk_ble_consumer_event_t *event, void *context) {
    (void)context;
    if (!event) return;
    os_critical_enter(&s_mailbox_lock);
    if ((int32_t)(event->session - s_session) < 0) {
        ++s_stats.dropped; os_critical_leave(&s_mailbox_lock); return;
    }
    if (event->session != s_session || event->kind == RK_BLE_CONSUMER_CANCEL) {
        s_session = event->session; ++s_cancel_revision; ++s_stats.cancelled;
        s_head = s_count = 0; s_faulted = false;
    }
    if (s_count == CONSUMER_MAILBOX_SIZE) {
        ++s_cancel_revision; ++s_stats.overflow; ++s_stats.cancelled;
        s_head = s_count = 0; s_faulted = true;
    }
    /* After loss, require a release before trusting another held-key history.
     * Drop that release too; it cannot authorize a gesture. */
    if (s_faulted && event->kind == RK_BLE_CONSUMER_EDGE && !event->pressed) s_faulted = false;
    else if (!s_faulted && event->kind != RK_BLE_CONSUMER_CANCEL) {
        s_mailbox[(s_head+s_count)%CONSUMER_MAILBOX_SIZE] = *event;
        ++s_count; ++s_stats.accepted;
    } else if (event->kind != RK_BLE_CONSUMER_CANCEL) ++s_stats.dropped;
    bool wake = !s_wake_queued;
    if (wake) s_wake_queued = true;
    os_critical_leave(&s_mailbox_lock);
    if (wake && !platform_task_post_to_ui(drain_on_ui, NULL)) {
        os_critical_enter(&s_mailbox_lock); s_wake_queued = false; os_critical_leave(&s_mailbox_lock);
    }
}
void controller_ble_input_stats(controller_ble_input_stats_t *out) {
    if (!out) return;
    os_critical_enter(&s_mailbox_lock); *out=s_stats; os_critical_leave(&s_mailbox_lock);
}
