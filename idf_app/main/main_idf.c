#include "app.h"
#include "platform/platform_http.h"
#include "platform/platform_input.h"
#include "platform/platform_time.h"
#include "platform_display_idf.h"
#include "roon_client.h"
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

static void test_http_connectivity(void) {
    ESP_LOGI(TAG, "=== Testing HTTP connectivity ===");

    // Test 1: Public internet endpoint
    ESP_LOGI(TAG, "Test 1: Trying public endpoint httpbin.org...");
    char *response = NULL;
    size_t response_len = 0;
    int result = platform_http_get("http://httpbin.org/get", &response, &response_len);
    if (result == 0 && response) {
        ESP_LOGI(TAG, "✓ Public HTTP works! Got %d bytes from httpbin.org", response_len);
        platform_http_free(response);
    } else {
        ESP_LOGE(TAG, "✗ Public HTTP FAILED - ESP32 HTTP client may be broken");
    }

    // Test 2: Local bridge endpoint
    ESP_LOGI(TAG, "Test 2: Trying local bridge http://192.168.1.213:8088/zones");
    response = NULL;
    response_len = 0;
    result = platform_http_get("http://192.168.1.213:8088/zones", &response, &response_len);
    if (result == 0 && response) {
        ESP_LOGI(TAG, "✓ Local bridge works! Got %d bytes", response_len);
        platform_http_free(response);
    } else {
        ESP_LOGE(TAG, "✗ Local bridge FAILED - network routing issue between ESP32 and local LAN");
    }


    // Test 3: Local bridge endpoint
    ESP_LOGI(TAG, "Test 3: Trying local audiolinux: http://192.168.1.12:5001/index.html");
    response = NULL;
    response_len = 0;
    result = platform_http_get("http://192.168.1.12:5001/index.html", &response, &response_len);
    if (result == 0 && response) {
        ESP_LOGI(TAG, "✓ Local audiolinux works! Got %d bytes", response_len);
        platform_http_free(response);
    } else {
        ESP_LOGE(TAG, "✗ Local audiolinux FAILED - network routing issue between ESP32 and local LAN");
    }

    ESP_LOGI(TAG, "=== HTTP connectivity test complete ===");
}

void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    // Notify UI about network events
    ui_network_on_event(evt, ip_opt);

    // Notify roon_client when network is ready
    if (evt == RK_NET_EVT_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected with IP: %s - enabling HTTP", ip_opt ? ip_opt : "unknown");

        // Run connectivity test to diagnose network issues
        test_http_connectivity();

        roon_client_set_network_ready(true);
    } else if (evt == RK_NET_EVT_FAIL) {
        roon_client_set_network_ready(false);
    }
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

    // Register network configuration menu BEFORE starting ui_loop task
    ui_network_register_menu();

    // Start WiFi (will use defaults from Kconfig or saved config)
    ESP_LOGI(TAG, "Starting WiFi...");
    wifi_mgr_start();

    // Initialize input (rotary encoder)
    platform_input_init();

    // Start application logic
    ESP_LOGI(TAG, "Starting app...");
    app_entry();

    // Create UI loop task LAST - with lower priority so it doesn't block
    ESP_LOGI(TAG, "Creating UI loop task");
    xTaskCreate(ui_loop_task, "ui_loop", 8192, NULL, 2, NULL);  // 8KB stack (LVGL theme needs more)

    ESP_LOGI(TAG, "Initialization complete");
}
