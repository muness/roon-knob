// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "crash_report.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_idf_version.h"
// IDF 6 removed the selectable dump format: flash dumps are always ELF.
#define HIPHI_DUMP_SUMMARY (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && \
    (ESP_IDF_VERSION_MAJOR >= 6 || CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF))
#include "nvs.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "platform/platform_identity.h"
#if HIPHI_DUMP_SUMMARY
#include "esp_core_dump.h"
#include "esp_partition.h"
#endif
static const char *capture_status = "not_checked";
const char *crash_report_capture_status(void) { return capture_status; }
static const char *const namespace_name = "rk_crash";
const char *crash_report_read(crash_record_t *record) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return "none";
    if (err != ESP_OK) return "storage_error";
    size_t size = sizeof(*record);
    err = nvs_get_blob(handle, "last", record, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return "none";
    if (err != ESP_OK) return err == ESP_ERR_NVS_INVALID_LENGTH ? "corrupt" : "storage_error";
    return crash_record_valid(record, size) ? "available" : "corrupt";
}
void crash_report_capture_boot(void) {
    static bool captured;
    if (captured) return;
    captured = true;
    capture_status = "no_new_crash";
    crash_record_t record = {0};
    record.reset_reason = (uint32_t)esp_reset_reason();
    snprintf(record.target, sizeof(record.target), "%s", platform_device_slug());
    snprintf(record.capture_version, sizeof(record.capture_version), "%s", esp_app_get_description()->version);
#if HIPHI_DUMP_SUMMARY
    // Older OTA layouts have no dump partition. Never access an assumed offset.
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (partition && esp_core_dump_image_check() == ESP_OK) {
        esp_core_dump_summary_t summary = {0};
        if (esp_core_dump_get_summary(&summary) != ESP_OK) {
            capture_status = "summary_failed"; return; // preserve the raw dump
        }
        record.has_dump = 1; record.pc = summary.exc_pc;
        memcpy(record.task, summary.exc_task, sizeof(summary.exc_task));
        snprintf(record.crashed_elf_sha, sizeof(record.crashed_elf_sha), "%.*s",
            (int)sizeof(summary.app_elf_sha256), (const char *)summary.app_elf_sha256);
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        record.depth = summary.exc_bt_info.depth > CRASH_BACKTRACE_MAX ? CRASH_BACKTRACE_MAX : summary.exc_bt_info.depth;
        for (unsigned i = 0; i < record.depth; ++i) record.backtrace[i] = summary.exc_bt_info.bt[i];
#endif
    }
#endif
    if (!record.has_dump && record.reset_reason != ESP_RST_PANIC &&
        record.reset_reason != ESP_RST_INT_WDT && record.reset_reason != ESP_RST_TASK_WDT &&
        record.reset_reason != ESP_RST_WDT) return; // normal boots keep the last crash
    crash_record_seal(&record);
    nvs_handle_t handle;
    capture_status = "storage_failed";
    if (nvs_open(namespace_name, NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t err = nvs_set_blob(handle, "last", &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
#if HIPHI_DUMP_SUMMARY
    // The saved summary is checksum checked before removing the only raw evidence.
    if (err == ESP_OK) {
        crash_record_t saved;
        capture_status = "readback_failed";
        if (!strcmp(crash_report_read(&saved), "available") && !memcmp(&saved, &record, sizeof(record))) {
            capture_status = record.has_dump ? "captured" : "reset_only";
            if (record.has_dump && esp_core_dump_image_erase() != ESP_OK)
                capture_status = "captured_dump_erase_failed";
        }
    }
#else
    if (err == ESP_OK) capture_status = "reset_only";
#endif
}
#else
void crash_report_capture_boot(void) {}
const char *crash_report_capture_status(void) { return "unsupported"; }
const char *crash_report_read(crash_record_t *record) { (void)record; return "unsupported"; }
#endif
