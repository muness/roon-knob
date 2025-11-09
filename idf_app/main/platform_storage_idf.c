#include "platform/platform_storage.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

static const char *TAG = "platform_storage";
static const char *NAMESPACE = "rk_cfg";
static const char *KEY = "cfg";

static void ensure_version(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    if (cfg->cfg_ver == 0) {
        cfg->cfg_ver = RK_CFG_CURRENT_VER;
    }
}

static esp_err_t open_ns(nvs_handle_t *handle, nvs_open_mode_t mode) {
    return nvs_open(NAMESPACE, mode, handle);
}

bool platform_storage_load(rk_cfg_t *out) {
    if (!out) {
        return false;
    }
    esp_err_t err;
    nvs_handle_t handle;
    err = open_ns(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "nvs open failed: %s", esp_err_to_name(err));
        }
        return false;
    }
    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, KEY, out, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*out)) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "nvs read failed: %s", esp_err_to_name(err));
        }
        memset(out, 0, sizeof(*out));
        return false;
    }
    ensure_version(out);
    strncpy(out->bridge_base, CONFIG_RK_DEFAULT_BRIDGE_BASE, sizeof(out->bridge_base) - 1);
    out->bridge_base[sizeof(out->bridge_base) - 1] = '\0';
    ESP_LOGI(TAG, "Loaded config: bridge=%s zone=%s (bridge from Kconfig, zone from NVS)",
             out->bridge_base, out->zone_id[0] ? out->zone_id : "(empty)");

    return true;
}

bool platform_storage_save(const rk_cfg_t *in) {
    if (!in) {
        return false;
    }
    rk_cfg_t copy = *in;
    ensure_version(&copy);
    esp_err_t err;
    nvs_handle_t handle;
    err = open_ns(&handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs open rw failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_blob(handle, KEY, &copy, sizeof(copy));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void platform_storage_defaults(rk_cfg_t *out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    // Use Kconfig default for bridge_base
    strncpy(out->bridge_base, CONFIG_RK_DEFAULT_BRIDGE_BASE, sizeof(out->bridge_base) - 1);
    // zone_id is left empty - user will select from available zones
    // Leave SSID/pass empty; wifi_manager will fill these from Kconfig
    ESP_LOGI(TAG, "Applied defaults from Kconfig: bridge=%s (zone will be selected from available zones)",
             CONFIG_RK_DEFAULT_BRIDGE_BASE);
    out->cfg_ver = RK_CFG_CURRENT_VER;
}

void platform_storage_reset_wifi_only(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    cfg->ssid[0] = '\0';
    cfg->pass[0] = '\0';
    platform_storage_save(cfg);
}
