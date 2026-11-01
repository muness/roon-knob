#include "frame_display_preferences.h"

#include <esp_log.h>
#include <nvs.h>
#include <stdatomic.h>

static const char *TAG = "frame_display_prefs";
static const char *NAMESPACE = "rk_frame";
static const char *SHOW_IP_KEY = "show_ip";

static atomic_bool s_full_cover = ATOMIC_VAR_INIT(false);

static atomic_bool s_show_ip = ATOMIC_VAR_INIT(true);

void frame_display_preferences_init(void) {
    nvs_handle_t handle;
    atomic_store_explicit(&s_full_cover, false, memory_order_release);
    uint8_t value = 1;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_u8(handle, SHOW_IP_KEY, &value);
        uint8_t full_cover = 0;
        if (nvs_get_u8(handle, "full_cover", &full_cover) != ESP_OK) full_cover = 0;
        atomic_store_explicit(&s_full_cover, full_cover == 1, memory_order_release);
        nvs_close(handle);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Could not read show-IP preference: %s",
                 esp_err_to_name(err));
    }
    atomic_store_explicit(&s_show_ip, value != 0, memory_order_release);
}

bool frame_display_preferences_show_ip(void) {
    return atomic_load_explicit(&s_show_ip, memory_order_acquire);
}

bool frame_display_preferences_set_show_ip(bool show) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open preferences: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_u8(handle, SHOW_IP_KEY, show ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save show-IP preference: %s",
                 esp_err_to_name(err));
        return false;
    }
    atomic_store_explicit(&s_show_ip, show, memory_order_release);
    return true;
}

bool frame_display_preferences_full_cover(void) {
    return atomic_load_explicit(&s_full_cover, memory_order_acquire);
}

bool frame_display_preferences_set_full_cover(bool full_cover) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    err = nvs_set_u8(handle, "full_cover", full_cover ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save artwork layout: %s", esp_err_to_name(err));
        return false;
    }
    atomic_store_explicit(&s_full_cover, full_cover, memory_order_release);
    return true;
}
