#include "ota_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "driver/uart.h"
#include <string.h>

#define TAG "OTA"
#define UART_NUM UART_NUM_1
#define OTA_BUFFER_SIZE 1024
#define OTA_TIMEOUT_MS 30000  // 30 second timeout for UART data

static const esp_partition_t *s_update_partition = NULL;

void ota_update_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    s_update_partition = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(TAG, "Running partition: %s at offset 0x%lx",
             running->label, (unsigned long)running->address);
    
    if (s_update_partition != NULL) {
        ESP_LOGI(TAG, "Next update partition: %s at offset 0x%lx",
                 s_update_partition->label, (unsigned long)s_update_partition->address);
    } else {
        ESP_LOGW(TAG, "No OTA update partition available");
    }
}

void ota_get_running_partition(char *partition_label, size_t partition_size)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && partition_label && partition_size > 0) {
        strncpy(partition_label, running->label, partition_size - 1);
        partition_label[partition_size - 1] = '\0';
    }
}

bool ota_update_from_uart(uint32_t image_size)
{
    esp_err_t err;
    esp_ota_handle_t ota_handle = 0;
    uint8_t *ota_buffer = NULL;
    uint32_t bytes_received = 0;
    bool success = false;

    ESP_LOGI(TAG, "Starting OTA update, image size: %lu bytes", (unsigned long)image_size);

    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "No update partition available");
        return false;
    }

    if (image_size == 0 || image_size > s_update_partition->size) {
        ESP_LOGE(TAG, "Invalid image size: %lu (partition size: %lu)",
                 (unsigned long)image_size, (unsigned long)s_update_partition->size);
        return false;
    }

    // Allocate buffer
    ota_buffer = malloc(OTA_BUFFER_SIZE);
    if (ota_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA buffer");
        return false;
    }

    // Begin OTA
    err = esp_ota_begin(s_update_partition, image_size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        free(ota_buffer);
        return false;
    }

    ESP_LOGI(TAG, "OTA begin successful, receiving firmware...");

    // Receive firmware data via UART
    while (bytes_received < image_size) {
        uint32_t remaining = image_size - bytes_received;
        uint32_t to_read = (remaining < OTA_BUFFER_SIZE) ? remaining : OTA_BUFFER_SIZE;
        
        // Read from UART with timeout
        int len = uart_read_bytes(UART_NUM, ota_buffer, to_read, pdMS_TO_TICKS(OTA_TIMEOUT_MS));
        
        if (len <= 0) {
            ESP_LOGE(TAG, "UART read timeout or error at %lu/%lu bytes",
                     (unsigned long)bytes_received, (unsigned long)image_size);
            esp_ota_abort(ota_handle);
            free(ota_buffer);
            return false;
        }

        // Write to OTA partition
        err = esp_ota_write(ota_handle, ota_buffer, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            free(ota_buffer);
            return false;
        }

        bytes_received += len;

        // Log progress every 10%
        if ((bytes_received % (image_size / 10)) < OTA_BUFFER_SIZE) {
            ESP_LOGI(TAG, "OTA progress: %lu/%lu bytes (%d%%)",
                     (unsigned long)bytes_received, (unsigned long)image_size,
                     (int)((bytes_received * 100) / image_size));
        }
    }

    ESP_LOGI(TAG, "OTA write complete, verifying...");

    // End OTA (verifies image)
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        }
        free(ota_buffer);
        return false;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        free(ota_buffer);
        return false;
    }

    ESP_LOGI(TAG, "OTA update successful! Next boot partition: %s", s_update_partition->label);
    success = true;

    free(ota_buffer);
    return success;
}
