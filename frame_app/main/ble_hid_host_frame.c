#include "ble_hid_host_frame.h"

#include "controller_ble_input.h"
#include "eink_ui.h"
#include "platform/platform_task.h"
#include "rk_ble_hid_host.h"

#include <esp_log.h>
#include <stdint.h>

static const char *TAG = "ble_hid_frame";

static void apply_connection_status(void *arg) {
    eink_ui_set_ble_status((bool)(intptr_t)arg);
}

static void on_status(const rk_ble_hid_host_status_t *status, void *context) {
    (void)context;
    if (!status) {
        return;
    }
    if (!platform_task_post_to_ui(apply_connection_status,
                                  (void *)(intptr_t)status->connected)) {
        ESP_LOGW(TAG, "UI queue full; BLE status update dropped");
    }
}

bool ble_hid_host_frame_start(void) {
    const rk_ble_hid_host_config_t config = {
        .default_enabled = true,
        .on_media_key = controller_ble_input_on_media_key,
        .on_status = on_status,
        .callback_context = NULL,
    };
    rk_ble_hid_host_result_t result = rk_ble_hid_host_init(&config);
    if (result != RK_BLE_HID_HOST_OK) {
        ESP_LOGE(TAG, "BLE HID host init rejected: %s",
                 rk_ble_hid_host_result_name(result));
        return false;
    }
    return true;
}
