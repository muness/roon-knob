// main_tough.c -- hiphi tough entry point
// Boot sequence: NVS -> AXP192/SPI/I2C display+touch bring-up -> LVGL ->
// touch_ui_init -> controller -> UI loop -> app_entry -> WiFi

#include "app.h"
#include "bridge_client.h"
#include "controller_config.h"
#include "captive_portal.h"
#include "platform_display_tough.h"
#include "platform/platform_identity.h"
#include "platform/platform_input.h"
#include "platform/platform_mdns.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"
#include "touch_ui.h"
#include "wifi_manager.h"

#include "lvgl.h"

#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <stdatomic.h>

static const char *TAG = "main";

#define UI_LOOP_STACK_SIZE 16384

static volatile bool s_mdns_init_pending = false;
/* The event loop produces this request; the UI task is the sole consumer
 * and sole initiator of the STA settings server (matches Frame's pattern). */
static atomic_bool s_sta_server_pending = ATOMIC_VAR_INIT(false);

static const char *config_durability_warning(void) {
  controller_config_snapshot_t config = {0};
  if (!controller_config_snapshot(&config)) {
    return NULL;
  }
  if (config.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT) {
    return "Settings saved but could\nnot be verified";
  }
  if (config.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY) {
    return "Settings storage unavailable;\nchanges may not survive";
  }
  return NULL;
}

static void post_runtime_network_status(const char *status) {
  const char *warning = config_durability_warning();
  touch_ui_post_network_status(warning ? warning : status);
}

static void show_config_durability_diagnostic(void) {
  controller_config_snapshot_t config = {0};
  if (!controller_config_snapshot(&config)) {
    return;
  }
  if (config.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT ||
      config.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY) {
    post_runtime_network_status(NULL);
  }
}

void rk_net_evt_cb(rk_net_evt_t evt, const char *ip_opt) {
  switch (evt) {
  case RK_NET_EVT_CONNECTING: {
    int retry = wifi_mgr_get_retry_count();
    ESP_LOGI(TAG, "WiFi: Connecting... (retry %d)", retry);
    if (retry == 0) {
      post_runtime_network_status("WiFi: Connecting...");
    }
    break;
  }

  case RK_NET_EVT_GOT_IP:
    ESP_LOGI(TAG, "WiFi connected with IP: %s", ip_opt ? ip_opt : "unknown");
    post_runtime_network_status(NULL);
    bridge_client_set_device_ip(ip_opt);
    bridge_client_set_network_ready(true);
    s_mdns_init_pending = true;
    atomic_store_explicit(&s_sta_server_pending, true, memory_order_release);
    break;

  case RK_NET_EVT_FAIL:
  case RK_NET_EVT_WRONG_PASSWORD:
  case RK_NET_EVT_NO_AP_FOUND:
  case RK_NET_EVT_AUTH_TIMEOUT: {
    int attempt = wifi_mgr_get_retry_count();
    int max = wifi_mgr_get_retry_max();
    const char *error = ip_opt ? ip_opt : "Connection failed";
    ESP_LOGW(TAG, "WiFi: %s, attempt %d/%d", error, attempt, max);
    char msg[64];
    snprintf(msg, sizeof(msg), "WiFi: %s (%d/%d)", error, attempt, max);
    post_runtime_network_status(msg);
    bridge_client_set_network_ready(false);
    break;
  }

  case RK_NET_EVT_AP_STARTED:
    ESP_LOGI(TAG, "WiFi: AP mode started (SSID: %s)", platform_provisioning_ssid());
    touch_ui_post_network_status("Connect to hiphi-tough-setup\nto configure WiFi");
    touch_ui_post_zone_name("WiFi Setup");
    bridge_client_set_network_ready(false);
    atomic_store_explicit(&s_sta_server_pending, false, memory_order_release);
    break;

  case RK_NET_EVT_AP_STOPPED:
    ESP_LOGI(TAG, "WiFi: AP mode stopped, connecting to network...");
    post_runtime_network_status("WiFi: Connecting...");
    atomic_store_explicit(&s_sta_server_pending, false, memory_order_release);
    break;

  default:
    break;
  }
}

static void ui_loop_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "UI loop task started");

  while (true) {
    platform_task_run_pending();
    platform_input_process_events();
    touch_ui_process();

    static bool s_mdns_initialized = false;
    if (s_mdns_init_pending) {
      s_mdns_init_pending = false;
      if (!s_mdns_initialized) {
        s_mdns_initialized = true;
        ESP_LOGI(TAG, "Initializing mDNS (network is up)...");
        platform_mdns_init(wifi_mgr_get_hostname());
      }
    }

    if (atomic_exchange_explicit(&s_sta_server_pending, false,
                                 memory_order_acq_rel)) {
      if (!wifi_mgr_is_ap_mode()) {
        ESP_LOGI(TAG, "Starting STA web server...");
        if (!captive_portal_start_sta()) {
          ESP_LOGE(TAG, "Failed to start STA web server; will retry on next network event");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "hiphi tough starting...");

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_err_t erase_err = nvs_flash_erase();
    if (erase_err != ESP_OK) {
      ESP_LOGW(TAG, "NVS erase failed, ignoring");
    }
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  ESP_LOGI(TAG, "Initializing display/touch hardware...");
  if (!platform_display_init()) {
    ESP_LOGE(TAG, "Display/touch hardware init failed!");
    return;
  }

  ESP_LOGI(TAG, "Initializing LVGL...");
  lv_init();

  ESP_LOGI(TAG, "Registering LVGL display/touch driver...");
  if (!platform_display_register_lvgl_driver()) {
    ESP_LOGE(TAG, "LVGL driver registration failed!");
    return;
  }

  ESP_LOGI(TAG, "Initializing UI...");
  touch_ui_init();

  // Install the controller action handler before touch/input can dispatch.
  app_controller_init();

  platform_input_init();

  ESP_LOGI(TAG, "Creating UI loop task");
  if (xTaskCreate(ui_loop_task, "ui_loop", UI_LOOP_STACK_SIZE, NULL, 2, NULL) != pdPASS) {
    ESP_LOGE(TAG, "FATAL: Failed to create UI loop task");
    return;
  }

  ESP_LOGI(TAG, "Starting app...");
  app_entry();
  show_config_durability_diagnostic();

  ESP_LOGI(TAG, "Starting WiFi...");
  wifi_mgr_start();

  ESP_LOGI(TAG, "Initialization complete");
}
