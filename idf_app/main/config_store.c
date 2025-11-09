#include "config_store.h"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

static const char *TAG = "config_store";
static const char *RK_CFG_NS = "rk_cfg";
static const char *RK_CFG_KEY = "cfg";
static const uint8_t RK_CFG_VER = 1;

static esp_err_t open_namespace(nvs_handle_t *handle, nvs_open_mode_t mode) {
    return nvs_open(RK_CFG_NS, mode, handle);
}

static void ensure_version(rk_cfg_t *cfg) {
    if (!cfg) {
        return;
    }
    if (cfg->cfg_ver == 0) {
        cfg->cfg_ver = RK_CFG_VER;
    }
}

bool rk_cfg_load(rk_cfg_t *out) {
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "open(ro) failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, RK_CFG_KEY, out, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*out)) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "load failed: %s", esp_err_to_name(err));
        }
        memset(out, 0, sizeof(*out));
        return false;
    }

    ensure_version(out);
    return out->ssid[0] != '\0';
}

bool rk_cfg_save(const rk_cfg_t *in) {
    if (!in) {
        return false;
    }
    rk_cfg_t copy = *in;
    ensure_version(&copy);

    nvs_handle_t handle;
    esp_err_t err = open_namespace(&handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open(rw) failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, RK_CFG_KEY, &copy, sizeof(copy));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void rk_cfg_reset_wifi_only(void) {
    rk_cfg_t cfg = {0};
    bool ok = rk_cfg_load(&cfg);
    if (!ok) {
        memset(&cfg, 0, sizeof(cfg));
    }
    cfg.ssid[0] = '\0';
    cfg.pass[0] = '\0';
    ensure_version(&cfg);
    rk_cfg_save(&cfg);
}
