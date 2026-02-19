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

static void ensure_version(rk_cfg_t *cfg) {
  if (!cfg) {
    return;
  }
  if (cfg->cfg_ver == 0) {
    cfg->cfg_ver = RK_CFG_CURRENT_VER;
  }
}

// Strip trailing slashes and whitespace from URL
static void strip_trailing_slashes(char *url) {
  if (!url)
    return;
  size_t len = strlen(url);
  // Strip trailing whitespace and slashes
  while (len > 0 &&
         (url[len - 1] == '/' || url[len - 1] == ' ' || url[len - 1] == '\t' ||
          url[len - 1] == '\n' || url[len - 1] == '\r')) {
    url[--len] = '\0';
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

  // First, query the actual stored size
  size_t stored_len = 0;
  err = nvs_get_blob(handle, KEY, NULL, &stored_len);
  if (err != ESP_OK) {
    nvs_close(handle);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGW(TAG, "nvs size query failed: %s", esp_err_to_name(err));
    }
    memset(out, 0, sizeof(*out));
    return false;
  }

  // Initialize output to zeros first
  memset(out, 0, sizeof(*out));

  // Read whatever size is stored
  size_t read_len = stored_len;
  err = nvs_get_blob(handle, KEY, out, &read_len);
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs read failed: %s", esp_err_to_name(err));
    memset(out, 0, sizeof(*out));
    return false;
  }

  // Handle migration from older config versions
  if (stored_len == RK_CFG_V1_SIZE && out->cfg_ver == 1) {
    ESP_LOGI(TAG, "Migrating config from v1 to v3");
    rk_cfg_set_display_defaults(out);
    // Migrate v1 ssid/pass into wifi[0]
    if (out->ssid[0]) {
      rk_cfg_add_wifi(out, out->ssid, out->pass);
    }
    out->cfg_ver = RK_CFG_CURRENT_VER;
  } else if (stored_len == RK_CFG_V2_SIZE && out->cfg_ver == 2) {
    ESP_LOGI(TAG, "Migrating config from v2 to v3");
    // Migrate v2 ssid/pass into wifi[0]
    if (out->ssid[0]) {
      rk_cfg_add_wifi(out, out->ssid, out->pass);
    }
    out->cfg_ver = RK_CFG_CURRENT_VER;
  } else if (stored_len != sizeof(*out)) {
    ESP_LOGW(TAG,
             "Config size mismatch (stored=%d, expected=%d), applying defaults",
             (int)stored_len, (int)sizeof(*out));
    rk_cfg_set_display_defaults(out);
    out->cfg_ver = RK_CFG_CURRENT_VER;
  }

  ensure_version(out);

  // Normalize bridge_base: strip trailing slashes to prevent //path issues
  strip_trailing_slashes(out->bridge_base);

  // Log all fields for debugging
  ESP_LOGI(TAG,
           "Loaded config: ssid='%s' bridge='%s' zone='%s' ver=%d rot=%d/%d",
           out->ssid[0] ? out->ssid : "(empty)",
           out->bridge_base[0] ? out->bridge_base : "(empty)",
           out->zone_id[0] ? out->zone_id : "(empty)", out->cfg_ver,
           out->rotation_charging, out->rotation_not_charging);

  return true;
}

bool platform_storage_save(const rk_cfg_t *in) {
  if (!in) {
    return false;
  }
  rk_cfg_t copy = *in;
  ensure_version(&copy);
  strip_trailing_slashes(copy.bridge_base);

  ESP_LOGI(TAG, "Saving config: ssid='%s' bridge='%s' zone='%s' ver=%d",
           copy.ssid[0] ? copy.ssid : "(empty)",
           copy.bridge_base[0] ? copy.bridge_base : "(empty)",
           copy.zone_id[0] ? copy.zone_id : "(empty)", copy.cfg_ver);

  esp_err_t err;
  nvs_handle_t handle;
  err = open_ns(&handle, NVS_READWRITE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs open rw failed: %s", esp_err_to_name(err));
    return false;
  }
  err = nvs_set_blob(handle, KEY, &copy, sizeof(copy));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
    nvs_close(handle);
    return false;
  }
  ESP_LOGI(TAG, "nvs_set_blob OK, committing...");

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    nvs_close(handle);
    return false;
  }
  ESP_LOGI(TAG, "nvs_commit OK");
  nvs_close(handle);

  // Verify by reading back
  rk_cfg_t verify = {0};
  if (!platform_storage_load(&verify)) {
    ESP_LOGE(TAG, "VERIFY FAILED: Could not read back saved config!");
    return false;
  }

  if (strcmp(verify.ssid, copy.ssid) != 0) {
    ESP_LOGE(TAG, "VERIFY FAILED: SSID mismatch! saved='%s' read='%s'",
             copy.ssid, verify.ssid);
    return false;
  }

  ESP_LOGI(TAG, "VERIFY OK: Config saved and verified successfully");
  return true;
}

void platform_storage_defaults(rk_cfg_t *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  rk_cfg_set_display_defaults(out);
  // Leave bridge_base empty - mDNS discovery is the primary method
  // wifi_manager will fill SSID/pass from Kconfig defaults
  // zone_id is left empty - user will select from available zones
  ESP_LOGI(TAG, "Applied defaults (bridge will be discovered via mDNS)");
  out->cfg_ver = RK_CFG_CURRENT_VER;
}

void platform_storage_reset_wifi_only(rk_cfg_t *cfg) {
  if (!cfg) {
    return;
  }
  cfg->ssid[0] = '\0';
  cfg->pass[0] = '\0';
  memset(cfg->wifi, 0, sizeof(cfg->wifi));
  cfg->wifi_count = 0;
  platform_storage_save(cfg);
}
