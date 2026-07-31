#include "controller_ble_input.h"

#include "controller_input.h"

#include <esp_log.h>

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
