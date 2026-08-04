#include "ble_hid_host_dial.h"

#include "controller_ble_input.h"
#include "platform/platform_task.h"
#include "rk_ble_hid_host.h"
#include "ui_network.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ble_hid_dial";

static void log_memory(const char *stage) {
    ESP_LOGI(TAG, "%s: internal free=%u largest=%u PSRAM free=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void apply_status_on_ui(void *arg) {
    char *text = arg;
    ui_network_set_ble_status(text);
    free(text);
}

static const char *state_name(rk_ble_hid_host_state_t state) {
    switch (state) {
    case RK_BLE_HID_HOST_STATE_UNAVAILABLE:
        return "Unavailable";
    case RK_BLE_HID_HOST_STATE_DISABLED:
        return "Disabled";
    case RK_BLE_HID_HOST_STATE_STARTING:
        return "Starting";
    case RK_BLE_HID_HOST_STATE_READY:
        return "Ready";
    case RK_BLE_HID_HOST_STATE_SCANNING:
        return "Scanning";
    case RK_BLE_HID_HOST_STATE_CONNECTING:
        return "Connecting";
    case RK_BLE_HID_HOST_STATE_CONNECTED:
        return "Connected";
    case RK_BLE_HID_HOST_STATE_STOPPING:
        return "Stopping";
    case RK_BLE_HID_HOST_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

static void on_status(const rk_ble_hid_host_status_t *status, void *context) {
    (void)context;
    if (!status) {
        return;
    }

    char text[96];
    const char *name =
        status->active_name[0] ? status->active_name : status->bonded_name;
    if (name[0] &&
        (status->state == RK_BLE_HID_HOST_STATE_CONNECTED ||
         status->state == RK_BLE_HID_HOST_STATE_CONNECTING)) {
        snprintf(text, sizeof(text), "%s: %s", state_name(status->state), name);
    } else {
        snprintf(text, sizeof(text), "%s", state_name(status->state));
    }
    const size_t length = strnlen(text, sizeof(text) - 1);
    char *copy = malloc(length + 1);
    if (!copy) {
        ESP_LOGW(TAG, "Could not allocate BLE status update");
        return;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    if (!platform_task_post_to_ui(apply_status_on_ui, copy)) {
        ESP_LOGW(TAG, "UI queue full; BLE status update dropped");
        free(copy);
    }
}

bool ble_hid_host_dial_start(void) {
    const rk_ble_hid_host_config_t config = {
        .default_enabled = false,
        .on_media_key = controller_ble_input_on_media_key,
        .on_status = on_status,
        .callback_context = NULL,
    };
    log_memory("before service allocation");
    rk_ble_hid_host_result_t result = rk_ble_hid_host_init(&config);
    log_memory(result == RK_BLE_HID_HOST_OK ? "after service allocation" :
                                               "service allocation rejected");
    if (result != RK_BLE_HID_HOST_OK) {
        ESP_LOGE(TAG, "BLE HID host init rejected: %s",
                 rk_ble_hid_host_result_name(result));
        return false;
    }
    return true;
}
