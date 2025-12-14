#include "ota_update.h"
#include "platform/platform_storage.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_app_desc.h"

static const char *TAG = "ota";

static ota_info_t s_ota_info = {0};
static TaskHandle_t s_ota_task = NULL;

// Get bridge base URL from storage
static bool get_bridge_url(char *url, size_t len) {
    rk_cfg_t cfg;
    if (platform_storage_load(&cfg)) {
        if (cfg.bridge_base[0]) {
            strncpy(url, cfg.bridge_base, len - 1);
            url[len - 1] = '\0';
            return true;
        }
    }
    return false;
}

const char* ota_get_current_version(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

int ota_compare_versions(const char* v1, const char* v2) {
    // Parse semver: major.minor.patch[-prerelease]
    // Per semver spec: 1.0.0-alpha < 1.0.0 (pre-release is lower than release)
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;

    // Skip 'v' prefix if present
    if (v1[0] == 'v' || v1[0] == 'V') v1++;
    if (v2[0] == 'v' || v2[0] == 'V') v2++;

    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);
    sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2);

    if (maj1 != maj2) return maj1 - maj2;
    if (min1 != min2) return min1 - min2;
    if (pat1 != pat2) return pat1 - pat2;

    // Same major.minor.patch - check pre-release suffix
    // A version with pre-release (-dev, -beta, -alpha, -rc) is LESS than one without
    const char *pre1 = strchr(v1, '-');
    const char *pre2 = strchr(v2, '-');

    if (pre1 && !pre2) return -1;  // v1 has pre-release, v2 doesn't: v1 < v2
    if (!pre1 && pre2) return 1;   // v1 doesn't have pre-release, v2 does: v1 > v2
    if (pre1 && pre2) return strcmp(pre1, pre2);  // Both have pre-release: compare lexically

    return 0;  // Identical
}

static void check_update_task(void *arg) {
    char bridge_url[128];
    char url[192];
    char response[256];

    s_ota_info.status = OTA_STATUS_CHECKING;
    strncpy(s_ota_info.current_version, ota_get_current_version(), sizeof(s_ota_info.current_version) - 1);

    if (!get_bridge_url(bridge_url, sizeof(bridge_url))) {
        ESP_LOGE(TAG, "No bridge URL configured");
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "No bridge configured", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    snprintf(url, sizeof(url), "%s/firmware/version", bridge_url);
    ESP_LOGI(TAG, "Checking for updates at %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Connection failed", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code == 404) {
        ESP_LOGI(TAG, "No firmware available on server");
        s_ota_info.status = OTA_STATUS_UP_TO_DATE;
        esp_http_client_cleanup(client);
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (status_code != 200 || content_length <= 0 || content_length >= (int)sizeof(response)) {
        ESP_LOGE(TAG, "Bad response: status=%d, len=%d", status_code, content_length);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Bad server response", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int read_len = esp_http_client_read(client, response, content_length);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Read failed", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    response[read_len] = '\0';

    // Parse simple JSON response: {"version": "X.Y.Z", "size": NNN}
    // Extract version string
    char *ver_start = strstr(response, "\"version\"");
    if (!ver_start) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Missing version", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ver_start = strchr(ver_start + 9, '"');  // Find opening quote of value
    if (!ver_start) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Invalid JSON", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ver_start++;  // Skip opening quote
    char *ver_end = strchr(ver_start, '"');
    if (!ver_end || (ver_end - ver_start) >= (int)sizeof(s_ota_info.available_version)) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Invalid version", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    size_t ver_len = ver_end - ver_start;
    memcpy(s_ota_info.available_version, ver_start, ver_len);
    s_ota_info.available_version[ver_len] = '\0';

    // Extract size (optional)
    char *size_start = strstr(response, "\"size\"");
    if (size_start) {
        size_start = strchr(size_start + 6, ':');
        if (size_start) {
            s_ota_info.firmware_size = (uint32_t)strtoul(size_start + 1, NULL, 10);
        }
    }

    // Compare versions
    int cmp = ota_compare_versions(s_ota_info.available_version, s_ota_info.current_version);
    if (cmp > 0) {
        ESP_LOGI(TAG, "Update available: %s -> %s", s_ota_info.current_version, s_ota_info.available_version);
        s_ota_info.status = OTA_STATUS_AVAILABLE;
    } else {
        ESP_LOGI(TAG, "Already up to date: %s", s_ota_info.current_version);
        s_ota_info.status = OTA_STATUS_UP_TO_DATE;
    }

    s_ota_task = NULL;
    vTaskDelete(NULL);
}

static void do_update_task(void *arg) {
    char bridge_url[128];
    char url[192];

    s_ota_info.status = OTA_STATUS_DOWNLOADING;
    s_ota_info.progress_percent = 0;

    if (!get_bridge_url(bridge_url, sizeof(bridge_url))) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "No bridge configured", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    snprintf(url, sizeof(url), "%s/firmware/download", bridge_url);
    ESP_LOGI(TAG, "Downloading firmware from %s", url);

    // Get the next OTA partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "No OTA partition", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Writing to partition: %s", update_partition->label);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Connection failed", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Invalid firmware", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_ota_info.firmware_size = content_length;

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "OTA begin failed", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    char *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        esp_http_client_cleanup(client);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Out of memory", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int total_read = 0;
    int read_len;

    while ((read_len = esp_http_client_read(client, buf, 4096)) > 0) {
        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            s_ota_info.status = OTA_STATUS_ERROR;
            strncpy(s_ota_info.error_msg, "Write failed", sizeof(s_ota_info.error_msg));
            s_ota_task = NULL;
            vTaskDelete(NULL);
            return;
        }

        total_read += read_len;
        s_ota_info.progress_percent = (total_read * 100) / content_length;

        // Yield to other tasks
        vTaskDelay(1);
    }

    free(buf);
    esp_http_client_cleanup(client);

    if (total_read != content_length) {
        ESP_LOGE(TAG, "Download incomplete: %d/%d", total_read, content_length);
        esp_ota_abort(ota_handle);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Download incomplete", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Validation failed", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Set boot failed", sizeof(s_ota_info.error_msg));
        s_ota_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA update complete! Rebooting in 2 seconds...");
    s_ota_info.status = OTA_STATUS_COMPLETE;
    s_ota_info.progress_percent = 100;

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    // Never reached
    s_ota_task = NULL;
    vTaskDelete(NULL);
}

void ota_init(void) {
    memset(&s_ota_info, 0, sizeof(s_ota_info));
    strncpy(s_ota_info.current_version, ota_get_current_version(), sizeof(s_ota_info.current_version) - 1);
    s_ota_info.status = OTA_STATUS_IDLE;
    ESP_LOGI(TAG, "OTA initialized, current version: %s", s_ota_info.current_version);
}

void ota_check_for_update(bool force) {
    if (s_ota_task != NULL) {
        ESP_LOGW(TAG, "OTA task already running");
        return;
    }

    // Skip OTA checks for development/beta versions (unless forced by user)
    if (!force) {
        const char *current = ota_get_current_version();
        if (strstr(current, "-dev") || strstr(current, "-beta") || strstr(current, "-alpha")) {
            ESP_LOGI(TAG, "Skipping OTA check for development version: %s", current);
            s_ota_info.status = OTA_STATUS_UP_TO_DATE;
            return;
        }
    }

    xTaskCreate(check_update_task, "ota_check", 8192, NULL, 1, &s_ota_task);  // Low priority to not block UI
}

void ota_start_update(void) {
    if (s_ota_task != NULL) {
        ESP_LOGW(TAG, "OTA task already running");
        return;
    }

    if (s_ota_info.status != OTA_STATUS_AVAILABLE) {
        ESP_LOGW(TAG, "No update available");
        return;
    }

    xTaskCreate(do_update_task, "ota_update", 8192, NULL, 1, &s_ota_task);  // Low priority to not block UI
}

const ota_info_t* ota_get_info(void) {
    return &s_ota_info;
}
