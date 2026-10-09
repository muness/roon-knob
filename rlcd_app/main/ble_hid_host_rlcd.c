#include "ble_hid_host_rlcd.h"

#include "controller_ble_input.h"
#include "platform/platform_task.h"
#include "rk_ble_hid_host.h"
#include "rlcd_ui.h"

#include <esp_log.h>
#include <stdint.h>

static const char *TAG = "ble_hid_rlcd";
static bool s_sleep_requested;
static void apply_status(void *arg) { rlcd_ui_set_ble_status((bool)(intptr_t)arg); }
static void on_status(const rk_ble_hid_host_status_t *status, void *context) {
    (void)context;
    if (!status || !platform_task_post_to_ui(apply_status,
                                               (void *)(intptr_t)status->connected)) {
        ESP_LOGW(TAG, "could not post BLE status");
    }
}
bool ble_hid_host_rlcd_start(void) {
    const rk_ble_hid_host_config_t config = {
        .default_enabled = true,
        .on_media_key = controller_ble_input_on_media_key,
        .on_status = on_status,
    };
    rk_ble_hid_host_result_t result = rk_ble_hid_host_init(&config);
    if (result != RK_BLE_HID_HOST_OK) {
        ESP_LOGE(TAG, "BLE HID init rejected: %s", rk_ble_hid_host_result_name(result));
        return false;
    }
    return true;
}

rlcd_ble_sleep_status_t ble_hid_host_rlcd_prepare_for_sleep(void) {
    rk_ble_hid_host_status_t status = {0};
    if (rk_ble_hid_host_status_copy(&status) != RK_BLE_HID_HOST_OK ||
        status.state == RK_BLE_HID_HOST_STATE_UNAVAILABLE ||
        status.state == RK_BLE_HID_HOST_STATE_ERROR) {
        return RLCD_BLE_SLEEP_FAILED;
    }
    if (status.quiesced_for_sleep) return RLCD_BLE_SLEEP_READY;
    if (!s_sleep_requested) {
        if (rk_ble_hid_host_prepare_for_sleep() != RK_BLE_HID_HOST_OK) {
            return RLCD_BLE_SLEEP_FAILED;
        }
        s_sleep_requested = true;
    }
    return RLCD_BLE_SLEEP_PENDING;
}

bool ble_hid_host_rlcd_cancel_sleep(void) {
    if (!s_sleep_requested) return true;
    if (rk_ble_hid_host_cancel_sleep() != RK_BLE_HID_HOST_OK) return false;
    s_sleep_requested = false;
    return true;
}
