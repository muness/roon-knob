// platform_storage.h implementation for tough_app (modeled on
// frame_app/main/platform_storage_frame.c, which itself diverged from the
// idf_app variant only in its log messages). The NVS namespace/key and V1/V2
// migration logic are identical across targets by design -- rk_cfg.h defines
// the shared compatibility DTO and its migration helpers once for all targets.

#include "platform/platform_storage.h"
#include "rk_cfg.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

static const char *TAG = "platform_storage";
static const char *NAMESPACE = "rk_cfg";
static const char *KEY = "cfg";

static esp_err_t open_ns(nvs_handle_t *handle, nvs_open_mode_t mode) {
    return nvs_open(NAMESPACE, mode, handle);
}

static void apply_storage_defaults(rk_cfg_t *out) {
    rk_cfg_set_storage_defaults(out);
}

bool platform_storage_read(rk_cfg_t *out,
                           platform_storage_read_result_t *out_result) {
    if (out_result) {
        *out_result = (platform_storage_read_result_t){
            .state = PLATFORM_STORAGE_READ_ERROR,
            .needs_persist = false,
        };
    }
    if (!out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    esp_err_t err;
    nvs_handle_t handle;
    err = open_ns(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (out_result) {
                out_result->state = PLATFORM_STORAGE_READ_EMPTY;
            }
            return true;
        }
        ESP_LOGW(TAG, "nvs open failed: %s", esp_err_to_name(err));
        return false;
    }

    size_t stored_len = 0;
    err = nvs_get_blob(handle, KEY, NULL, &stored_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (out_result) {
                out_result->state = PLATFORM_STORAGE_READ_EMPTY;
            }
            return true;
        }
        ESP_LOGW(TAG, "nvs size query failed: %s", esp_err_to_name(err));
        return false;
    }

    size_t read_len = sizeof(*out);  // Buffer capacity, not stored blob size
    err = nvs_get_blob(handle, KEY, out, &read_len);
    nvs_close(handle);

    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            ESP_LOGW(TAG, "Config blob too large for struct (stored=%d, max=%d)",
                     (int)stored_len, (int)sizeof(*out));
            apply_storage_defaults(out);
            if (out_result) {
                out_result->state = PLATFORM_STORAGE_READ_RECOVERED;
                out_result->needs_persist = true;
            }
            return true;
        }
        ESP_LOGW(TAG, "nvs read failed: %s", esp_err_to_name(err));
        memset(out, 0, sizeof(*out));
        return false;
    }

    // Handle this Tough's local config upgrade.
    if (stored_len == RK_CFG_V1_SIZE && out->cfg_ver == 1) {
        ESP_LOGI(TAG, "Upgrading this Tough's config from v1 to v3");
    } else if (stored_len == RK_CFG_V2_SIZE && out->cfg_ver == 2) {
        ESP_LOGI(TAG, "Upgrading this Tough's config from v2 to v3");
    } else if (stored_len != sizeof(*out)) {
        ESP_LOGW(TAG, "Config size mismatch (stored=%d, expected=%d)", (int)stored_len, (int)sizeof(*out));
    }

    bool needs_persist = rk_cfg_prepare_storage_read(out, stored_len);

    ESP_LOGI(TAG, "Loaded config: ssid='%s' bridge='%s' zone='%s' ver=%d",
             out->ssid[0] ? out->ssid : "(empty)",
             out->bridge_base[0] ? out->bridge_base : "(empty)",
             out->zone_id[0] ? out->zone_id : "(empty)", out->cfg_ver);
    if (out_result) {
        out_result->state = needs_persist ? PLATFORM_STORAGE_READ_RECOVERED
                                          : PLATFORM_STORAGE_READ_VALID;
        out_result->needs_persist = needs_persist;
    }
    return true;
}

platform_storage_write_result_t platform_storage_write(const rk_cfg_t *in) {
    if (!in) {
        return PLATFORM_STORAGE_NOT_COMMITTED;
    }
    rk_cfg_t copy;
    if (!rk_cfg_canonicalize_v3(&copy, in)) {
        return PLATFORM_STORAGE_NOT_COMMITTED;
    }

    ESP_LOGI(TAG, "Saving config: ssid='%s' bridge='%s' zone='%s' ver=%d",
             copy.ssid[0] ? copy.ssid : "(empty)",
             copy.bridge_base[0] ? copy.bridge_base : "(empty)",
             copy.zone_id[0] ? copy.zone_id : "(empty)", copy.cfg_ver);

    esp_err_t err;
    nvs_handle_t handle;
    err = open_ns(&handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs open rw failed: %s", esp_err_to_name(err));
        return PLATFORM_STORAGE_NOT_COMMITTED;
    }
    err = nvs_set_blob(handle, KEY, &copy, sizeof(copy));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return PLATFORM_STORAGE_NOT_COMMITTED;
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return PLATFORM_STORAGE_NOT_COMMITTED;
    }
    nvs_close(handle);

    rk_cfg_t verify = {0};
    platform_storage_read_result_t verify_result;
    if (!platform_storage_read(&verify, &verify_result) ||
        verify_result.state != PLATFORM_STORAGE_READ_VALID ||
        verify_result.needs_persist) {
        ESP_LOGE(TAG, "VERIFY FAILED: Could not read back saved config!");
        return PLATFORM_STORAGE_COMMITTED_UNVERIFIED;
    }
    /* Canonicalize only the readback. copy is already canonical, so comparison
     * remains independent of structure padding with one fewer compatibility
     * record on the caller's task stack than rk_cfg_v3_equal(). */
    if (!rk_cfg_v3_equal_to_canonical(&verify, &copy)) {
        ESP_LOGE(TAG, "VERIFY FAILED: full V3 candidate mismatch!");
        return PLATFORM_STORAGE_COMMITTED_UNVERIFIED;
    }

    return PLATFORM_STORAGE_COMMITTED_VERIFIED;
}

void platform_storage_defaults(rk_cfg_t *out) {
    if (!out) return;
    apply_storage_defaults(out);
    ESP_LOGI(TAG, "Applied defaults");
}
