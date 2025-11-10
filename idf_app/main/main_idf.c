#include "app.h"
#include "platform/platform_http.h"
#include "platform/platform_input.h"
#include "platform/platform_power.h"
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

static void battery_monitor_task(void *arg);

static void report_http_result(const char *label, int result, const char *url, size_t len) {
    if (result == 0) {
        ESP_LOGI(TAG, "✓ %s (%s) -> %d bytes", label, url, len);
    } else {
        ESP_LOGE(TAG, "✗ %s (%s) failed (%d)", label, url, result);
    }
}

static void test_http_connectivity(void) {
    ESP_LOGI(TAG, "=== Testing HTTP connectivity ===");
    ui_set_message("Testing connection to bridge...");

    const struct {
        const char *label;
        const char *url;
    } tests[] = {
        { "Public endpoint", "http://httpbin.org/get" },
        { "Bridge /zones", "http://192.168.1.2:8088/zones" },
        { "Bridge /status", "http://192.168.1.2:8088/status" },
        { "Bridge /now_playing/mock", "http://192.168.1.2:8088/now_playing/mock" },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        ESP_LOGI(TAG, "Test %zu: %s", i + 1, tests[i].label);
        char *response = NULL;
        size_t response_len = 0;
        int result = platform_http_get(tests[i].url, &response, &response_len);
        report_http_result(tests[i].label, result, tests[i].url, response_len);
        platform_http_free(response);
    }

    ESP_LOGI(TAG, "=== HTTP connectivity test complete ===");
}

static void battery_monitor_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Battery monitor task started");
    const TickType_t delay = pdMS_TO_TICKS(CONFIG_RK_BATTERY_SAMPLE_PERIOD_MS);
    TickType_t interval = delay > 0 ? delay : 1;
    struct platform_power_status status;

    while (true) {
        if (platform_power_get_status(&status)) {
            ui_set_battery_status(status.present, status.percentage, status.voltage_mv, status.charging);
        } else {
            ui_set_battery_status(false, -1, 0, false);
        }
        vTaskDelay(interval);
    }
}

void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    // Notify UI about network events
    ui_network_on_event(evt, ip_opt);

    if (evt == RK_NET_EVT_CONNECTING) {
        ui_set_message("Initializing Wi-Fi...");
    }

    // Notify roon_client when network is ready
    if (evt == RK_NET_EVT_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected with IP: %s - enabling HTTP", ip_opt ? ip_opt : "unknown");

        // Run connectivity test to diagnose network issues
        test_http_connectivity();

        ui_set_message("Bridge is ready");

        roon_client_set_network_ready(true);
    } else if (evt == RK_NET_EVT_FAIL) {
        ui_set_message("Network unavailable");
        roon_client_set_network_ready(false);
    }
}

static void ui_loop_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "UI loop task started");
    while (true) {
        // Process queued input events from ISR context
        platform_input_process_events();

        // Run LVGL task handler
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

    // Start WiFi (will use defaults from Kconfig or saved config)
    ESP_LOGI(TAG, "Starting WiFi...");
    wifi_mgr_start();

    // Initialize input (rotary encoder)
    platform_input_init();

    ESP_LOGI(TAG, "Initializing power monitor...");
    platform_power_init();

    // Start application logic
    ESP_LOGI(TAG, "Starting app...");
    app_entry();

    // Create UI loop task LAST - with lower priority so it doesn't block
    ESP_LOGI(TAG, "Creating UI loop task");
    xTaskCreate(ui_loop_task, "ui_loop", 8192, NULL, 2, NULL);  // 8KB stack (LVGL theme needs more)

    ESP_LOGI(TAG, "Creating battery monitor task");
    xTaskCreate(battery_monitor_task, "battery_mon", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "Initialization complete");
}
