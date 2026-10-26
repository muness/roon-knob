// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "ota_update.h"
#include "bridge_client.h"
#include "platform/platform_identity.h"
#include "platform/platform_task.h"
#include "os_critical.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"

static const char *TAG = "ota";

/* Only the admitted worker mutates its working result. Readers get a copy of
 * the separately published result under the short admission lock. */
static ota_info_t s_ota_info;
static ota_info_t s_published;
static bridge_request_endpoint_t s_result_endpoint;
static os_critical_t s_ota_lock = OS_CRITICAL_INITIALIZER;
typedef enum { OTA_JOB_CHECK, OTA_JOB_UPDATE, OTA_JOB_INSTALL } ota_job_kind_t;
typedef struct {
    ota_job_kind_t kind;
    bridge_request_endpoint_t endpoint;
    char from_version[32];
    char selected_version[32];
} ota_job_t;
static bool s_initialized, s_ota_busy, s_have_pending;
static ota_job_t s_active_job, s_pending_job;
static void ota_publish(void) {
    os_critical_enter(&s_ota_lock);
    s_published = s_ota_info;
    os_critical_leave(&s_ota_lock);
}
static bool ota_job_current(const ota_job_t *job) {
    return bridge_client_request_endpoint_current(&job->endpoint) &&
        (!job->from_version[0] || !strcmp(job->from_version, ota_get_current_version()));
}


#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

/* An OTA image is installed only when it is the same product as the running one. The chip and
 * flash geometry come from this build's own configuration, and the product from the running
 * image's descriptor, so no target can be handed a sibling's binary just because the bridge
 * served it. Nothing here authenticates the firmware; it rejects accidental mismatches. */
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define OTA_TARGET_CHIP_ID ESP_CHIP_ID_ESP32S3
#elif defined(CONFIG_IDF_TARGET_ESP32)
#define OTA_TARGET_CHIP_ID ESP_CHIP_ID_ESP32
#else
#error "OTA is not configured for this chip"
#endif
#if defined(CONFIG_ESPTOOLPY_FLASHSIZE_16MB)
#define OTA_TARGET_FLASH_SIZE ESP_IMAGE_FLASH_SIZE_16MB
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_8MB)
#define OTA_TARGET_FLASH_SIZE ESP_IMAGE_FLASH_SIZE_8MB
#else
#error "OTA is not configured for this flash size"
#endif
#define OTA_IMAGE_PREFIX_SIZE (sizeof(esp_image_header_t) + \
                               sizeof(esp_image_segment_header_t) + \
                               sizeof(esp_app_desc_t))

static void ota_set_identity_headers(esp_http_client_handle_t client) {
    char knob_id[16];
    platform_http_get_knob_id(knob_id, sizeof(knob_id));
    esp_http_client_set_header(client, "X-Knob-Id", knob_id);
    esp_http_client_set_header(client, "X-Knob-Version", ota_get_current_version());
    esp_http_client_set_header(client, "X-Device-Type", platform_device_slug());
}

/* The historical Dial project name stays valid for the Dial only. */
static bool ota_project_accepted(const char *image_project) {
    const esp_app_desc_t *running = esp_app_get_description();
    if (strncmp(running->project_name, image_project, sizeof(running->project_name)) == 0) return true;
    return strcmp(running->project_name, "hiphi_dial") == 0 && strcmp(image_project, "roon_knob") == 0;
}

static bool ota_image_matches_running(const void *prefix) {
    esp_image_header_t image;
    esp_image_segment_header_t segment;
    esp_app_desc_t app;
    const unsigned char *bytes = prefix;
    memcpy(&image, bytes, sizeof(image));
    memcpy(&segment, bytes + sizeof(image), sizeof(segment));
    memcpy(&app, bytes + sizeof(image) + sizeof(segment), sizeof(app));
    return image.magic == ESP_IMAGE_HEADER_MAGIC &&
           image.segment_count > 0 && image.segment_count <= ESP_IMAGE_MAX_SEGMENTS &&
           image.chip_id == OTA_TARGET_CHIP_ID &&
           image.spi_size == OTA_TARGET_FLASH_SIZE &&
           segment.data_len >= sizeof(app) &&
           app.magic_word == ESP_APP_DESC_MAGIC_WORD &&
           memchr(app.project_name, '\0', sizeof(app.project_name)) != NULL &&
           ota_project_accepted(app.project_name);
}

/* The downloaded bytes have to be the release the bridge advertised, newer than what is running,
 * and on the same channel. The version endpoint is metadata about a different request; only the
 * descriptor inside the image says what would actually be flashed. */
static const char *ota_skip_v(const char *version) {
    return (version[0] == 'v' || version[0] == 'V') ? version + 1 : version;
}

static bool ota_image_version_acceptable(const void *prefix) {
    esp_app_desc_t app;
    memcpy(&app, (const unsigned char *)prefix + sizeof(esp_image_header_t) +
                     sizeof(esp_image_segment_header_t), sizeof(app));
    if (memchr(app.version, '\0', sizeof(app.version)) == NULL) return false;
    const char *image = ota_skip_v(app.version);
    const char *advertised = ota_skip_v(s_ota_info.available_version);
    const char *running = ota_get_current_version();
    if (image[0] == '\0' || strcmp(image, advertised) != 0) return false;
    if (ota_compare_versions(image, running) <= 0) return false;
    return strcmp(ota_channel_for_version(image), ota_channel_for_version(running)) == 0;
}

/* Prerelease builds are published to their own channel. Asking the Stable feed would never
 * offer them anything, so the channel follows the running version. */
const char *ota_channel_for_version(const char *version) {
    const char *pre = strchr(version, '-');
    if (!pre) return "stable";
    if (strncmp(pre, "-beta", 5) == 0 || strncmp(pre, "-rc", 3) == 0) return "beta";
    return "alpha";
}

bool ota_confirm_boot(void) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) ESP_LOGW(TAG, "Could not confirm the running image: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

const char* ota_get_current_version(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

static int ota_prerelease_stage(const char *pre) {
    if (strncmp(pre, "-dev", 4) == 0) return 0;
    if (strncmp(pre, "-alpha", 6) == 0) return 1;
    if (strncmp(pre, "-beta", 5) == 0) return 2;
    if (strncmp(pre, "-rc", 3) == 0) return 3;
    return 1;
}

static int ota_prerelease_number(const char *pre) {
    const char *dot = strchr(pre, '.');
    return dot ? atoi(dot + 1) : 0;
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

    // Same major.minor.patch: a pre-release is lower than the release it precedes, and
    // pre-releases order by stage (dev < alpha < beta < rc) and then numerically.
    const char *pre1 = strchr(v1, '-');
    const char *pre2 = strchr(v2, '-');

    if (pre1 && !pre2) return -1;
    if (!pre1 && pre2) return 1;
    if (pre1 && pre2) {
        int stage1 = ota_prerelease_stage(pre1), stage2 = ota_prerelease_stage(pre2);
        if (stage1 != stage2) return stage1 - stage2;
        return ota_prerelease_number(pre1) - ota_prerelease_number(pre2);
    }

    return 0;  // Identical
}

/* The check, update and install bodies are plain functions so the task wrappers stay trivial and
 * the whole sequence runs under the host test harness. Each returns with s_ota_info holding the
 * outcome; none of them changes worker ownership. */
static void ota_perform_check(const ota_job_t *job) {
    char bridge_url[128];
    char url[192];
    char response[256];

    s_ota_info.status = OTA_STATUS_CHECKING;
    ota_publish();
    strncpy(s_ota_info.current_version, ota_get_current_version(), sizeof(s_ota_info.current_version) - 1);

    if (!ota_job_current(job)) {
        ESP_LOGE(TAG, "No Unified Hi-Fi Control URL configured");
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "No Hi-Fi Control configured",
                sizeof(s_ota_info.error_msg));
        return;
    }

    snprintf(bridge_url, sizeof(bridge_url), "%s", job->endpoint.base);
    snprintf(url, sizeof(url), "%s/firmware/version?channel=%s", bridge_url,
             ota_channel_for_version(ota_get_current_version()));
    ESP_LOGI(TAG, "Checking for updates at %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ota_set_identity_headers(client);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Connection failed", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code == 404) {
        ESP_LOGI(TAG, "No firmware available on server");
        s_ota_info.status = OTA_STATUS_UP_TO_DATE;
        esp_http_client_cleanup(client);
        return;
    }

    if (status_code != 200 || content_length <= 0 || content_length >= (int)sizeof(response)) {
        ESP_LOGE(TAG, "Bad response: status=%d, len=%d", status_code, content_length);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Bad server response", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        return;
    }

    int read_len = esp_http_client_read(client, response, content_length);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Read failed", sizeof(s_ota_info.error_msg));
        return;
    }
    response[read_len] = '\0';

    // Parse simple JSON response: {"version": "X.Y.Z", "size": NNN}
    // Extract version string
    char *ver_start = strstr(response, "\"version\"");
    if (!ver_start) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Missing version", sizeof(s_ota_info.error_msg));
        return;
    }
    ver_start = strchr(ver_start + 9, '"');  // Find opening quote of value
    if (!ver_start) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Invalid JSON", sizeof(s_ota_info.error_msg));
        return;
    }
    ver_start++;  // Skip opening quote
    char *ver_end = strchr(ver_start, '"');
    if (!ver_end || (ver_end - ver_start) >= (int)sizeof(s_ota_info.available_version)) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Invalid version", sizeof(s_ota_info.error_msg));
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

}

static void ota_perform_update(const ota_job_t *job) {
    char bridge_url[128];
    char url[192];

    s_ota_info.status = OTA_STATUS_DOWNLOADING;
    s_ota_info.progress_percent = 0;
    ota_publish();

    if (!ota_job_current(job)) {
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "No Hi-Fi Control configured",
                sizeof(s_ota_info.error_msg));
        return;
    }

    snprintf(bridge_url, sizeof(bridge_url), "%s", job->endpoint.base);
    snprintf(url, sizeof(url), "%s/firmware/download?channel=%s", bridge_url,
             ota_channel_for_version(ota_get_current_version()));
    ESP_LOGI(TAG, "Downloading firmware from %s", url);

    // Get the next OTA partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "No OTA partition", sizeof(s_ota_info.error_msg));
        return;
    }

    ESP_LOGI(TAG, "Writing to partition: %s", update_partition->label);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ota_set_identity_headers(client);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Connection failed", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200 || content_length < (int)OTA_IMAGE_PREFIX_SIZE ||
        (size_t)content_length > update_partition->size) {
        ESP_LOGE(TAG, "Invalid firmware response: status=%d length=%d", status_code, content_length);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Invalid firmware", sizeof(s_ota_info.error_msg));
        esp_http_client_cleanup(client);
        return;
    }

    s_ota_info.firmware_size = content_length;
    char *buf = malloc(4096);
    if (!buf) {
        esp_http_client_cleanup(client);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Out of memory", sizeof(s_ota_info.error_msg));
        return;
    }

    /* HTTP reads can split the descriptor at any byte. Accumulate the complete
     * prefix and validate it before esp_ota_begin erases any flash. */
    size_t prefix_read = 0;
    while (prefix_read < OTA_IMAGE_PREFIX_SIZE) {
        int count = esp_http_client_read(client, buf + prefix_read,
                                        OTA_IMAGE_PREFIX_SIZE - prefix_read);
        if (count <= 0) break;
        prefix_read += (size_t)count;
    }
    if (prefix_read != OTA_IMAGE_PREFIX_SIZE || !ota_image_matches_running(buf)) {
        free(buf);
        esp_http_client_cleanup(client);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Firmware target mismatch", sizeof(s_ota_info.error_msg));
        return;
    }
    if (!ota_image_version_acceptable(buf)) {
        free(buf);
        esp_http_client_cleanup(client);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Firmware version mismatch", sizeof(s_ota_info.error_msg));
        return;
    }

    /* Erase admission: a bridge replaced during metadata/download cannot grant
     * an install. After this point the captured image transaction may finish. */
    if (!ota_job_current(job)) {
        free(buf); esp_http_client_cleanup(client);
        s_ota_info.status = OTA_STATUS_ERROR;
        snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Update request expired");
        return;
    }
    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, content_length, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        free(buf);
        esp_http_client_cleanup(client);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "OTA begin failed", sizeof(s_ota_info.error_msg));
        return;
    }
    err = esp_ota_write(ota_handle, buf, prefix_read);
    if (err != ESP_OK) {
        free(buf);
        esp_ota_abort(ota_handle);
        esp_http_client_cleanup(client);
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Write failed", sizeof(s_ota_info.error_msg));
        return;
    }

    int total_read = (int)prefix_read;
    int read_len;

    while ((read_len = esp_http_client_read(client, buf, 4096)) > 0) {
        if (read_len > content_length - total_read) {
            free(buf);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            s_ota_info.status = OTA_STATUS_ERROR;
            strncpy(s_ota_info.error_msg, "Invalid firmware length", sizeof(s_ota_info.error_msg));
            return;
        }
        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            s_ota_info.status = OTA_STATUS_ERROR;
            strncpy(s_ota_info.error_msg, "Write failed", sizeof(s_ota_info.error_msg));
            return;
        }

        total_read += read_len;
        s_ota_info.progress_percent = (total_read * 100) / content_length;
        ota_publish();

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
        return;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Validation failed", sizeof(s_ota_info.error_msg));
        return;
    }

    if (!ota_job_current(job)) {
        s_ota_info.status = OTA_STATUS_ERROR;
        snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Update request expired");
        return;
    }
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        s_ota_info.status = OTA_STATUS_ERROR;
        strncpy(s_ota_info.error_msg, "Set boot failed", sizeof(s_ota_info.error_msg));
        return;
    }

    ESP_LOGI(TAG, "OTA update complete! Rebooting in 2 seconds...");
    s_ota_info.status = OTA_STATUS_COMPLETE;
    s_ota_info.progress_percent = 100;
    ota_publish();

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

}

/* The bridge asked for an install: always re-check first (forced, so a prerelease device is not
 * skipped), and install only an image ota_compare_versions says is strictly newer. Whatever the
 * bridge claimed, nothing older or equal is ever written. */
static void ota_perform_install(const ota_job_t *job) {
    ota_perform_check(job);
    if (s_ota_info.status != OTA_STATUS_AVAILABLE) return;
    if (ota_compare_versions(s_ota_info.available_version, ota_get_current_version()) <= 0) {
        s_ota_info.status = OTA_STATUS_UP_TO_DATE;
        return;
    }
    ota_perform_update(job);
}

static void ota_worker(void *arg) {
    (void)arg;
    for (;;) {
        os_critical_enter(&s_ota_lock);
        ota_job_t job = s_active_job;
        os_critical_leave(&s_ota_lock);
        memset(&s_ota_info, 0, sizeof(s_ota_info));
        snprintf(s_ota_info.current_version, sizeof(s_ota_info.current_version), "%s", ota_get_current_version());
        snprintf(s_ota_info.available_version, sizeof(s_ota_info.available_version), "%s", job.selected_version);
        if (!ota_job_current(&job)) {
            s_ota_info.status = OTA_STATUS_ERROR;
            snprintf(s_ota_info.error_msg, sizeof(s_ota_info.error_msg), "Update request expired");
        } else if (job.kind == OTA_JOB_CHECK) {
            ota_perform_check(&job);
        } else if (job.kind == OTA_JOB_UPDATE) {
            ota_perform_update(&job);
        } else {
            ota_perform_install(&job);
        }
        /* Publication, pending transfer and idle are one transaction. A caller
         * either joins this worker or reserves a new one after it publishes idle. */
        os_critical_enter(&s_ota_lock);
        s_published = s_ota_info;
        s_result_endpoint = job.endpoint;
        bool again = s_have_pending;
        if (again) {
            s_active_job = s_pending_job;
            s_have_pending = false;
        } else {
            s_ota_busy = false;
        }
        os_critical_leave(&s_ota_lock);
        if (!again) break;
    }
    platform_task_finish();
}

static bool same_install(const ota_job_t *a, const ota_job_t *b) {
    return a->kind == OTA_JOB_INSTALL && b->kind == OTA_JOB_INSTALL &&
        a->endpoint.endpoint_generation == b->endpoint.endpoint_generation &&
        a->endpoint.network_epoch == b->endpoint.network_epoch &&
        !strcmp(a->endpoint.base, b->endpoint.base) && !strcmp(a->from_version, b->from_version);
}

static ota_request_result_t ota_submit(ota_job_kind_t kind, const char *from) {
    ota_job_t job = {.kind = kind};
    size_t n = from ? strlen(from) : 0;
    if (n >= sizeof(job.from_version) || (n && strcmp(from, ota_get_current_version())))
        return OTA_REQUEST_STALE;
    if (from) memcpy(job.from_version, from, n + 1);
    if (!bridge_client_capture_request_endpoint(&job.endpoint)) return OTA_REQUEST_STALE;
    os_critical_enter(&s_ota_lock);
    if (s_ota_busy) {
        ota_request_result_t result = OTA_REQUEST_BUSY;
        if (same_install(&job, &s_active_job) ||
            (s_have_pending && same_install(&job, &s_pending_job))) {
            result = OTA_REQUEST_COALESCED;
        } else if (kind == OTA_JOB_INSTALL && !s_have_pending) {
            s_pending_job = job; s_have_pending = true; result = OTA_REQUEST_ACCEPTED;
        }
        os_critical_leave(&s_ota_lock);
        return result;
    }
    if (kind == OTA_JOB_UPDATE) {
        if (s_published.status != OTA_STATUS_AVAILABLE ||
            job.endpoint.endpoint_generation != s_result_endpoint.endpoint_generation ||
            job.endpoint.network_epoch != s_result_endpoint.network_epoch ||
            strcmp(job.endpoint.base, s_result_endpoint.base)) {
            os_critical_leave(&s_ota_lock); return OTA_REQUEST_STALE;
        }
        memcpy(job.selected_version, s_published.available_version, sizeof(job.selected_version));
    }
    /* Reserve before creation: the task may run and finish before create returns. */
    s_ota_busy = true;
    s_active_job = job;
    os_critical_leave(&s_ota_lock);
    if (platform_task_start_internal_stack("ota_worker", 8192, ota_worker, NULL) == 0)
        return OTA_REQUEST_ACCEPTED;
    os_critical_enter(&s_ota_lock);
    s_ota_busy = s_have_pending = false;
    s_published.status = OTA_STATUS_ERROR;
    snprintf(s_published.error_msg, sizeof(s_published.error_msg), "Could not start update");
    os_critical_leave(&s_ota_lock);
    return OTA_REQUEST_START_FAILED;
}

/* A freshly installed image is only trusted once it has stayed up for a while. A crash or watchdog
 * reset inside this window leaves the image unconfirmed and the bootloader returns to the previous
 * one (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE). It does not depend on Wi-Fi or the bridge, so a
 * network problem never rolls back a good image. */
#define OTA_CONFIRM_DELAY_MS 45000


static void confirm_boot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(OTA_CONFIRM_DELAY_MS));
    ota_confirm_boot();
    platform_task_finish();
}

void ota_init(void) {
    os_critical_enter(&s_ota_lock);
    bool first = !s_initialized;
    s_initialized = true;
    if (first) {
        memset(&s_published, 0, sizeof(s_published));
        snprintf(s_published.current_version, sizeof(s_published.current_version), "%s", ota_get_current_version());
        s_published.status = OTA_STATUS_IDLE;
    }
    os_critical_leave(&s_ota_lock);
    if (first) {
        /* Boot confirmation also writes flash: use an internal stack. */
        if (platform_task_start_internal_stack("ota_confirm", 3072, confirm_boot_task, NULL) != 0) {
            ESP_LOGW(TAG, "Could not start boot confirmation");
            /* Initialization remains idempotent even if confirmation cannot start.
             * Leave the image unconfirmed for bootloader recovery. */
        }
    }
}

void ota_check_for_update(bool force) {
    const char *current = ota_get_current_version();
    if (!force && (strstr(current, "-dev") || strstr(current, "-beta") || strstr(current, "-alpha"))) return;
    (void)ota_submit(OTA_JOB_CHECK, NULL);
}

void ota_start_update(void) { (void)ota_submit(OTA_JOB_UPDATE, NULL); }

ota_request_result_t ota_request_install_result(const char *from_version) {
    return ota_submit(OTA_JOB_INSTALL, from_version);
}
bool ota_request_install(const char *from_version) {
    ota_request_result_t result = ota_request_install_result(from_version);
    return result == OTA_REQUEST_ACCEPTED || result == OTA_REQUEST_COALESCED;
}

bool ota_get_info(ota_info_t *out) {
    if (!out) return false;
    os_critical_enter(&s_ota_lock);
    *out = s_published;
    os_critical_leave(&s_ota_lock);
    return true;
}
