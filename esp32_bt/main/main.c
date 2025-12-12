/**
 * @file main.c
 * @brief ESP32 Bluetooth firmware for Roon Knob
 *
 * This firmware runs on the ESP32 chip and provides:
 * - Classic Bluetooth AVRCP Controller (receive metadata, send commands)
 * - UART communication with ESP32-S3 (binary TLV protocol)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bt_avrcp.h"
#include "uart_protocol.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Bluetooth firmware starting...");

    // Initialize NVS (required for Bluetooth bonding)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize UART protocol (communication with S3)
    ESP_LOGI(TAG, "Initializing UART protocol...");
    uart_protocol_init();

    // Initialize Bluetooth AVRCP
    ESP_LOGI(TAG, "Initializing Bluetooth AVRCP...");
    bt_avrcp_init();

    ESP_LOGI(TAG, "Initialization complete. Waiting for commands...");

    // Main loop - most work happens in callbacks and tasks
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
