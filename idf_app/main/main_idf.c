#include "app.h"
#include "battery.h"
#include "config_server.h"
#include "display_sleep.h"
#include "font_manager.h"
#include "ota_update.h"
#include "platform/platform_http.h"
#include "platform/platform_input.h"
#include "platform/platform_mdns.h"
#include "platform/platform_storage.h"
#include "platform/platform_time.h"
#include "platform_display_idf.h"
#include "bridge_client.h"
#include "ui.h"
#include "ui_network.h"
#include "wifi_manager.h"

#include "lvgl.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <stdio.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";

// UI task handle for display sleep management
static TaskHandle_t g_ui_task_handle = NULL;

// Deferred operations flags (set in event handler, processed in UI task)
static volatile bool s_ota_check_pending = false;
static volatile bool s_config_server_start_pending = false;
static volatile bool s_config_server_stop_pending = false;
static volatile bool s_mdns_init_pending = false;

// WiFi retry message alternation
static esp_timer_handle_t s_wifi_msg_timer = NULL;
static char s_wifi_error_msg[48] = {0};   // e.g., "Wrong password"
static char s_wifi_retry_msg[48] = {0};   // e.g., "Retry 2/5"
static bool s_wifi_show_error = true;     // Toggle between error and retry

static void wifi_msg_toggle_cb(void *arg) {
    (void)arg;
    s_wifi_show_error = !s_wifi_show_error;
    // Update main display (line1), not just the status bar
    ui_update(s_wifi_show_error ? s_wifi_error_msg : s_wifi_retry_msg,
              "", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
}

static void start_wifi_msg_alternation(const char *error, int attempt, int max) {
    // Store both messages
    snprintf(s_wifi_error_msg, sizeof(s_wifi_error_msg), "WiFi: %s", error);
    snprintf(s_wifi_retry_msg, sizeof(s_wifi_retry_msg), "WiFi: Retry %d/%d", attempt, max);
    s_wifi_show_error = true;

    // Show error message first in main display
    ui_update(s_wifi_error_msg, "", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);

    // Create timer if needed
    if (!s_wifi_msg_timer) {
        esp_timer_create_args_t args = {
            .callback = wifi_msg_toggle_cb,
            .name = "wifi_msg",
        };
        esp_timer_create(&args, &s_wifi_msg_timer);
    }

    // Start periodic timer (600ms)
    esp_timer_stop(s_wifi_msg_timer);
    esp_timer_start_periodic(s_wifi_msg_timer, 600 * 1000);  // microseconds
}

static void stop_wifi_msg_alternation(void) {
    if (s_wifi_msg_timer) {
        esp_timer_stop(s_wifi_msg_timer);
    }
}

void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    // Notify UI about network events (uses lv_async_call internally)
    ui_network_on_event(evt, ip_opt);

    switch (evt) {
    case RK_NET_EVT_CONNECTING: {
        int retry = wifi_mgr_get_retry_count();
        ESP_LOGI(TAG, "WiFi: Connecting... (retry %d)", retry);
        // Only show "Connecting..." on first attempt; during retries, keep showing error/retry
        if (retry == 0) {
            stop_wifi_msg_alternation();
            ui_update("WiFi: Connecting...", "", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
        }
        break;
    }

    case RK_NET_EVT_GOT_IP:
        ESP_LOGI(TAG, "WiFi connected with IP: %s", ip_opt ? ip_opt : "unknown");
        stop_wifi_msg_alternation();
        ui_update("WiFi: Connected", "", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
        bridge_client_set_device_ip(ip_opt);  // Store IP for bridge recovery messages
        bridge_client_set_network_ready(true);
        // Defer heavy operations to UI task (sys_evt has limited stack)
        s_mdns_init_pending = true;  // mDNS needs network up first
        s_ota_check_pending = true;
        s_config_server_start_pending = true;
        break;

    // All failure events alternate between error reason and retry count
    case RK_NET_EVT_FAIL:
    case RK_NET_EVT_WRONG_PASSWORD:
    case RK_NET_EVT_NO_AP_FOUND:
    case RK_NET_EVT_AUTH_TIMEOUT: {
        int attempt = wifi_mgr_get_retry_count();
        int max = wifi_mgr_get_retry_max();
        const char *error = ip_opt ? ip_opt : "Connection failed";
        ESP_LOGW(TAG, "WiFi: %s, attempt %d/%d", error, attempt, max);
        start_wifi_msg_alternation(error, attempt, max);
        bridge_client_set_network_ready(false);
        break;
    }

    case RK_NET_EVT_AP_STARTED:
        ESP_LOGI(TAG, "WiFi: AP mode started (SSID: roon-knob-setup)");
        stop_wifi_msg_alternation();
        // Show setup instructions in main display area (line2 is top, line1 is bottom)
        ui_update("roon-knob-setup", "Connect to WiFi:", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
        ui_set_zone_name("WiFi Setup");
        bridge_client_set_network_ready(false);
        s_config_server_stop_pending = true;  // Stop config server in AP mode
        break;

    case RK_NET_EVT_AP_STOPPED:
        ESP_LOGI(TAG, "WiFi: AP mode stopped, connecting to network...");
        ui_update("WiFi: Connecting...", "", false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
        break;

    default:
        break;
    }
}

static void check_ota_status(void) {
    static ota_status_t last_status = OTA_STATUS_IDLE;
    static int last_progress = -1;

    const ota_info_t *info = ota_get_info();

    // Update UI when status changes
    if (info->status != last_status) {
        ESP_LOGI(TAG, "OTA status change: %d -> %d", last_status, info->status);
        last_status = info->status;

        switch (info->status) {
            case OTA_STATUS_IDLE:
                ESP_LOGI(TAG, "OTA: Idle");
                break;
            case OTA_STATUS_CHECKING:
                ESP_LOGI(TAG, "OTA: Checking for updates...");
                break;
            case OTA_STATUS_AVAILABLE:
                ESP_LOGI(TAG, "OTA: Update available: %s", info->available_version);
                ui_set_update_available(info->available_version);
                break;
            case OTA_STATUS_UP_TO_DATE:
                ESP_LOGI(TAG, "OTA: Firmware is up to date");
                ui_set_update_available(NULL);
                break;
            case OTA_STATUS_DOWNLOADING:
                ESP_LOGI(TAG, "OTA: Downloading update...");
                ui_set_update_progress(0);
                break;
            case OTA_STATUS_COMPLETE:
                ESP_LOGI(TAG, "OTA: Update complete, rebooting...");
                ui_set_message("Update complete! Rebooting...");
                break;
            case OTA_STATUS_ERROR:
                ESP_LOGE(TAG, "OTA: Error: %s", info->error_msg);
                ui_set_message(info->error_msg);
                ui_set_update_available(NULL);
                break;
            default:
                ESP_LOGW(TAG, "OTA: Unknown status %d", info->status);
                break;
        }
    }

    // Update progress during download (and keep display awake)
    if (info->status == OTA_STATUS_DOWNLOADING) {
        display_activity_detected();  // Keep display awake during OTA
        if (info->progress_percent != last_progress) {
            last_progress = info->progress_percent;
            ui_set_update_progress(info->progress_percent);
            ESP_LOGI(TAG, "OTA progress: %d%%", info->progress_percent);
        }
    }
}

static void ui_loop_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "UI loop task started");

    uint32_t ota_check_counter = 0;

    while (true) {
        // Process queued input events from ISR context
        platform_input_process_events();

        // Process pending display actions (e.g., swipe gestures)
        platform_display_process_pending();

        // Run LVGL task handler
        ui_loop_iter();

        // Check OTA status periodically (every 500ms = 50 iterations at 10ms)
        if (++ota_check_counter >= 50) {
            ota_check_counter = 0;
            check_ota_status();
        }

        // Check stack usage periodically (every 60 seconds = 6000 iterations at 10ms)
        static uint32_t stack_check_counter = 0;
        if (++stack_check_counter >= 6000) {
            stack_check_counter = 0;
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
            uint32_t free_bytes = hwm * sizeof(StackType_t);
            uint32_t used_bytes = 32768 - free_bytes;  // 32KB total stack
            ESP_LOGI(TAG, "ui_loop stack usage: %u/%u bytes (peak usage, %u free)",
                     (unsigned int)used_bytes, 32768, (unsigned int)free_bytes);
        }

        // Process deferred operations (from WiFi event callback)
        if (s_mdns_init_pending) {
            s_mdns_init_pending = false;
            ESP_LOGI(TAG, "Initializing mDNS (network is up)...");
            platform_mdns_init(wifi_mgr_get_hostname());
        }
        if (s_ota_check_pending) {
            s_ota_check_pending = false;
            ESP_LOGI(TAG, "Checking for firmware updates...");
            ota_check_for_update(false);  // Auto-check: skip for dev versions
        }
        if (s_config_server_start_pending) {
            s_config_server_start_pending = false;
            config_server_start();
        }
        if (s_config_server_stop_pending) {
            s_config_server_stop_pending = false;
            config_server_stop();
        }

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

    // Initialize battery monitoring
    ESP_LOGI(TAG, "Initializing battery monitoring...");
    if (!battery_init()) {
        ESP_LOGW(TAG, "Battery monitoring init failed, continuing without it");
    }

    // Initialize OTA update module
    ESP_LOGI(TAG, "Initializing OTA update module...");
    ota_init();

    // Initialize LVGL library
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_init();

    // Register LVGL display driver and start LVGL task
    ESP_LOGI(TAG, "Registering LVGL display driver...");
    if (!platform_display_register_lvgl_driver()) {
        ESP_LOGE(TAG, "Display driver registration failed!");
        return;
    }

    // Initialize font manager (pre-rendered bitmap fonts for Unicode support)
    ESP_LOGI(TAG, "Initializing font manager...");
    if (!font_manager_init()) {
        ESP_LOGW(TAG, "Font manager init failed - using built-in ASCII fonts");
    }

    // Now safe to initialize UI (depends on LVGL display being registered)
    ESP_LOGI(TAG, "Initializing UI...");
    ui_init();

    // Initialize input (rotary encoder)
    platform_input_init();

    // Create UI loop task BEFORE starting WiFi (WiFi events need LVGL task running)
    ESP_LOGI(TAG, "Creating UI loop task");
    xTaskCreate(ui_loop_task, "ui_loop", 32768, NULL, 2, &g_ui_task_handle);  // 32KB stack (LVGL + gzip decompression)

    // Initialize display sleep management now that UI task is created
    ESP_LOGI(TAG, "Initializing display sleep management");
    platform_display_init_sleep(g_ui_task_handle);

    // Start application logic
    ESP_LOGI(TAG, "Starting app...");
    app_entry();

    // Start WiFi AFTER UI task is running (WiFi event callbacks use lv_async_call)
    // Always start WiFi - we always boot into Roon mode now
    ESP_LOGI(TAG, "Starting WiFi...");
    wifi_mgr_start();

    ESP_LOGI(TAG, "Initialization complete");
}
