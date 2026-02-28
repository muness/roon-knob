#pragma once

#include <stdbool.h>
#include <stdint.h>

// OTA update status
typedef enum {
    OTA_STATUS_IDLE,
    OTA_STATUS_CHECKING,
    OTA_STATUS_AVAILABLE,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_COMPLETE,
    OTA_STATUS_ERROR,
    OTA_STATUS_UP_TO_DATE,
} ota_status_t;

// OTA update info
typedef struct {
    char current_version[32];
    char available_version[32];
    uint32_t firmware_size;
    ota_status_t status;
    int progress_percent;
    char error_msg[64];
} ota_info_t;

// Initialize OTA module
void ota_init(void);

// Check for updates (non-blocking, runs in background)
// force=true bypasses the dev/beta/alpha version skip
void ota_check_for_update(bool force);

// Start firmware update (non-blocking, runs in background)
void ota_start_update(void);

// Get current OTA status
const ota_info_t* ota_get_info(void);

// Get current app version string
const char* ota_get_current_version(void);

// Compare versions: returns >0 if v1 > v2, <0 if v1 < v2, 0 if equal
int ota_compare_versions(const char* v1, const char* v2);
