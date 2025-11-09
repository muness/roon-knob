#include "app.h"
#include "platform/platform_input.h"
#include "platform/platform_time.h"
#include "platform_display_idf.h"
#include "ui.h"
#include "ui_network.h"
#include "wifi_manager.h"

#include "lvgl.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";

void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    ui_network_on_event(evt, ip_opt);
}

static void ui_loop_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "UI loop task started");
    while (true) {
        ui_loop_iter();
        // Yield to lower priority tasks including IDLE
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Roon Knob starting...");

    // Initialize NVS for configuration storage
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGW(TAG, "NVS erase failed, ignoring");
        }
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialize display hardware (SPI, LCD panel) BEFORE lv_init
    ESP_LOGI(TAG, "Initializing display hardware...");
    if (!platform_display_init()) {
        ESP_LOGE(TAG, "Display hardware init failed!");
        return;
    }

    // Initialize LVGL library
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    // Register LVGL display driver and start LVGL task
    ESP_LOGI(TAG, "Registering LVGL display driver...");
    if (!platform_display_register_lvgl_driver()) {
        ESP_LOGE(TAG, "Display driver registration failed!");
        return;
    }

    // Now safe to initialize UI (depends on LVGL display being registered)
    ESP_LOGI(TAG, "Initializing UI...");
    ui_init();

    // Create UI loop task with sufficient stack BEFORE creating more UI elements
    ESP_LOGI(TAG, "Creating UI loop task");
    xTaskCreate(ui_loop_task, "ui_loop", 4096, NULL, 5, NULL);

    // Give UI loop task a moment to start
    vTaskDelay(pdMS_TO_TICKS(100));

    // Register network configuration menu (creates LVGL objects)
    ui_network_register_menu();

    // Start WiFi (will use defaults from Kconfig or saved config)
    ESP_LOGI(TAG, "Starting WiFi...");
    wifi_mgr_start();

    // Initialize input (rotary encoder)
    platform_input_init();

    // Start application logic
    ESP_LOGI(TAG, "Starting app...");
    app_entry();

    ESP_LOGI(TAG, "Initialization complete");
}
