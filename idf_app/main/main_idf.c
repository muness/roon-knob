#include "app.h"
#include "battery.h"
#include "ble_hid_client.h"
#include "config_server.h"
#include "controller_mode.h"
#include "display_sleep.h"
#include "ota_update.h"
#include "platform/platform_http.h"
#include "platform/platform_input.h"
#include "platform/platform_mdns.h"
#include "platform/platform_storage.h"
#include "platform/platform_time.h"
#include "platform_display_idf.h"
#include "roon_client.h"
#include "ui.h"
#include "ui_network.h"
#include "wifi_manager.h"

#include "lvgl.h"

#include <esp_err.h>
#include <esp_log.h>
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
static volatile bool s_ble_start_pending = false;
static volatile bool s_ble_stop_pending = false;

// BLE HID state change callback
static void ble_state_callback(ble_hid_state_t state, const char *device_name) {
    ESP_LOGI(TAG, "BLE state changed: %d, device: %s", state, device_name ? device_name : "none");

    // Map BLE state to UI state
    ui_ble_state_t ui_state;
    switch (state) {
        case BLE_HID_STATE_ADVERTISING:
            ui_state = UI_BLE_STATE_ADVERTISING;
            break;
        case BLE_HID_STATE_CONNECTED:
            ui_state = UI_BLE_STATE_CONNECTED;
            break;
        default:
            ui_state = UI_BLE_STATE_DISABLED;
            break;
    }
    ui_set_ble_status(ui_state, device_name);
}

// Input handler wrapper for BLE mode
static void ble_input_handler(ui_input_event_t event) {
    ble_hid_client_handle_input(event);
}

// Controller mode change callback
static void mode_change_callback(controller_mode_t new_mode, void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Controller mode changed to: %s", controller_mode_name(new_mode));

    if (new_mode == CONTROLLER_MODE_BLUETOOTH) {
        // Switch to BLE mode
        ui_set_ble_mode(true);
        ui_set_input_handler(ble_input_handler);
        s_ble_start_pending = true;
        // Stop Roon client when in BLE mode
        roon_client_set_network_ready(false);
    } else {
        // Switch to Roon mode
        ui_set_ble_mode(false);
        ui_set_input_handler(roon_client_handle_input);
        s_ble_stop_pending = true;
        // Re-enable Roon client if network is available
        char ip[16];
        if (wifi_mgr_get_ip(ip, sizeof(ip))) {
            roon_client_set_network_ready(true);
        }
    }
}

// Test bridge connectivity and show result to user
static bool test_bridge_connectivity(void) {
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    if (cfg.bridge_base[0] == '\0') {
        ESP_LOGI(TAG, "No bridge configured, will discover via mDNS");
        ui_set_message("Bridge: Searching...");
        return false;  // No bridge to test yet
    }

    ESP_LOGI(TAG, "Testing bridge: %s", cfg.bridge_base);
    ui_set_message("Bridge: Testing...");

    // Test /zones endpoint
    char url[256];
    snprintf(url, sizeof(url), "%s/zones", cfg.bridge_base);

    char *response = NULL;
    size_t response_len = 0;
    int result = platform_http_get(url, &response, &response_len);
    platform_http_free(response);

    if (result == 0 && response_len > 0) {
        ESP_LOGI(TAG, "✓ Bridge reachable: %s (%zu bytes)", cfg.bridge_base, response_len);
        ui_set_message("Bridge: Connected");
        return true;
    } else {
        ESP_LOGW(TAG, "✗ Bridge unreachable: %s (error %d)", cfg.bridge_base, result);
        ui_set_message("Bridge: Unreachable");
        return false;
    }
}

void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
    // Notify UI about network events (uses lv_async_call internally)
    ui_network_on_event(evt, ip_opt);

    switch (evt) {
    case RK_NET_EVT_CONNECTING:
        ESP_LOGI(TAG, "WiFi: Connecting...");
        ui_set_message("WiFi: Connecting...");
        break;

    case RK_NET_EVT_GOT_IP:
        ESP_LOGI(TAG, "WiFi connected with IP: %s", ip_opt ? ip_opt : "unknown");
        ui_set_message("WiFi: Connected");
        roon_client_set_network_ready(true);
        // Defer heavy operations to UI task (sys_evt has limited stack)
        s_mdns_init_pending = true;  // mDNS needs network up first
        s_ota_check_pending = true;
        s_config_server_start_pending = true;
        break;

    case RK_NET_EVT_FAIL:
        ESP_LOGW(TAG, "WiFi: Connection failed, retrying...");
        ui_set_message("WiFi: Retrying...");
        roon_client_set_network_ready(false);
        break;

    case RK_NET_EVT_AP_STARTED:
        ESP_LOGI(TAG, "WiFi: AP mode started (SSID: roon-knob-setup)");
        ui_set_message("Setup: roon-knob-setup");
        roon_client_set_network_ready(false);
        s_config_server_stop_pending = true;  // Stop config server in AP mode
        // Note: BLE fallback removed - WiFi and BLE can't coexist well on boot
        // User can manually switch to Bluetooth mode via zone picker
        break;

    case RK_NET_EVT_AP_STOPPED:
        ESP_LOGI(TAG, "WiFi: AP mode stopped, connecting to network...");
        ui_set_message("WiFi: Connecting...");
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

        // Process deferred operations (from WiFi event callback)
        if (s_mdns_init_pending) {
            s_mdns_init_pending = false;
            ESP_LOGI(TAG, "Initializing mDNS (network is up)...");
            platform_mdns_init(NULL);
        }
        if (s_ota_check_pending) {
            s_ota_check_pending = false;
            ESP_LOGI(TAG, "Checking for firmware updates...");
            ota_check_for_update();
        }
        if (s_config_server_start_pending) {
            s_config_server_start_pending = false;
            config_server_start();
        }
        if (s_config_server_stop_pending) {
            s_config_server_stop_pending = false;
            config_server_stop();
        }
        if (s_ble_start_pending) {
            s_ble_start_pending = false;
            // Must stop WiFi before starting BLE - they share the radio
            ESP_LOGI(TAG, "Stopping WiFi for BLE mode...");
            wifi_mgr_stop();
            vTaskDelay(pdMS_TO_TICKS(100));  // Give WiFi time to clean up
            ESP_LOGI(TAG, "Starting BLE HID...");
            if (ble_hid_client_start()) {
                ble_hid_client_set_state_callback(ble_state_callback);
                ESP_LOGI(TAG, "BLE HID started successfully");
            } else {
                ESP_LOGE(TAG, "BLE HID start failed");
                ui_set_message("BLE: Start failed");
            }
        }
        if (s_ble_stop_pending) {
            s_ble_stop_pending = false;
            ESP_LOGI(TAG, "Stopping BLE HID...");
            ble_hid_client_stop();
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

    // Now safe to initialize UI (depends on LVGL display being registered)
    ESP_LOGI(TAG, "Initializing UI...");
    ui_init();

    // Initialize input (rotary encoder)
    platform_input_init();

    // Create UI loop task BEFORE starting WiFi (WiFi events need LVGL task running)
    ESP_LOGI(TAG, "Creating UI loop task");
    xTaskCreate(ui_loop_task, "ui_loop", 8192, NULL, 2, &g_ui_task_handle);  // 8KB stack (LVGL theme needs more)

    // Initialize display sleep management now that UI task is created
    ESP_LOGI(TAG, "Initializing display sleep management");
    platform_display_init_sleep(g_ui_task_handle);

    // Initialize controller mode and register callback for mode changes
    ESP_LOGI(TAG, "Initializing controller mode...");
    controller_mode_init();
    controller_mode_register_callback(mode_change_callback, NULL);

    // Start application logic
    ESP_LOGI(TAG, "Starting app...");
    app_entry();

    // Start WiFi AFTER UI task is running (WiFi event callbacks use lv_async_call)
    // Skip WiFi if starting in Bluetooth mode (BLE and WiFi coexist poorly on boot)
    if (controller_mode_get() != CONTROLLER_MODE_BLUETOOTH) {
        ESP_LOGI(TAG, "Starting WiFi...");
        wifi_mgr_start();
    } else {
        ESP_LOGI(TAG, "Skipping WiFi - Bluetooth mode active");
    }

    ESP_LOGI(TAG, "Initialization complete");
}
