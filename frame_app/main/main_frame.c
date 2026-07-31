// main_frame.c — hiphi frame entry point
// Boot sequence: NVS → PMIC → e-ink display → eink_ui_init → input → UI loop → app_entry → WiFi

#include "app.h"
#include "ble_hid_host_frame.h"
#include "bridge_client.h"
#include "controller_config.h"
#include "captive_portal.h"
#include "eink_display.h"
#include "eink_ui.h"
#include "pmic_axp2101.h"
#include "platform/platform_http.h"
#include "platform/platform_input.h"
#include "platform/platform_mdns.h"
#include "platform/platform_task.h"
#include "platform/platform_time.h"
#include "wifi_manager.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <stdatomic.h>

static const char *TAG = "main";

#define UI_LOOP_STACK_SIZE 16384

// Deferred operations flags
static volatile bool s_mdns_init_pending = false;
static volatile bool s_ble_init_pending = false;
/* The event loop produces this request; the e-ink UI task is the sole
 * consumer and sole initiator of the STA settings server. */
static atomic_bool s_sta_server_pending = ATOMIC_VAR_INIT(false);
// Guard against double-init on WiFi reconnect
static bool s_ble_initialized = false;

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
  eink_ui_post_network_status(warning ? warning : status);
}

static void show_config_durability_diagnostic(void) {
  controller_config_snapshot_t config = {0};
  if (!controller_config_snapshot(&config)) {
    return;
  }
  if (config.durability == CONTROLLER_CONFIG_DURABILITY_DEGRADED_COMMIT) {
    post_runtime_network_status(NULL);
  } else if (config.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY) {
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
    post_runtime_network_status("WiFi: Connected");
    bridge_client_set_device_ip(ip_opt);
    bridge_client_set_network_ready(true);
    s_mdns_init_pending = true;
    s_ble_init_pending = true;
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
    ESP_LOGI(TAG, "WiFi: AP mode started (SSID: hiphi-frame-setup)");
    eink_ui_post_network_status("WiFi Setup: Connect to\nhiphi-frame-setup");
    eink_ui_post_zone_name("WiFi Setup");
    bridge_client_set_network_ready(false);
    /* The provisioning adapter has stopped the shared STA server before the
     * AP portal started. Discard any stale deferred STA-start request. */
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
    // Process bridge_client callbacks (status, zones, track info, etc.)
    platform_task_run_pending();

    // Process queued input events
    platform_input_process_events();

    // Process pending e-ink UI updates (debounced refresh)
    eink_ui_process();

    // Deferred mDNS init (needs network up, and stack space)
    static bool s_mdns_initialized = false;
    if (s_mdns_init_pending) {
      s_mdns_init_pending = false;
      if (!s_mdns_initialized) {
        s_mdns_initialized = true;
        ESP_LOGI(TAG, "Initializing mDNS (network is up)...");
        platform_mdns_init(wifi_mgr_get_hostname());
      }
    }

    // Deferred BLE init (after WiFi STA connects — coexistence safe)
    if (s_ble_init_pending) {
      s_ble_init_pending = false;
      if (!s_ble_initialized) {
        ESP_LOGI(TAG, "Initializing BLE remote...");
        s_ble_initialized = ble_hid_host_frame_start();
        if (!s_ble_initialized) {
          ESP_LOGE(TAG, "BLE remote initialization failed");
        }
      }
    }

    // Deferred STA web server (zone picker + BLE config)
    if (atomic_exchange_explicit(&s_sta_server_pending, false,
                                 memory_order_acq_rel)) {
      if (!wifi_mgr_is_ap_mode()) {
        ESP_LOGI(TAG, "Starting STA web server...");
        if (!captive_portal_start_sta()) {
          ESP_LOGE(TAG, "Failed to start STA web server; will retry on next network event");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "hiphi frame starting...");

  // Initialize NVS
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

  // Initialize PMIC first — enables ALDO power rails needed by e-ink panel
  ESP_LOGI(TAG, "Initializing PMIC...");
  if (!pmic_init()) {
    ESP_LOGW(TAG, "PMIC init failed, continuing without battery monitoring");
  }
  vTaskDelay(pdMS_TO_TICKS(100));  // Let power rails stabilize

  // Initialize e-ink display hardware
  ESP_LOGI(TAG, "Initializing e-ink display...");
  if (!eink_display_init()) {
    ESP_LOGE(TAG, "E-ink display init failed!");
    return;
  }

  // Initialize e-ink UI (draws boot screen)
  ESP_LOGI(TAG, "Initializing UI...");
  eink_ui_init();

  // Install the controller action handler before the input/UI actor starts.
  app_controller_init();

  // Initialize button input
  platform_input_init();

  // Create UI loop task (processes input + e-ink refreshes)
  ESP_LOGI(TAG, "Creating UI loop task");
  if (xTaskCreate(ui_loop_task, "ui_loop", UI_LOOP_STACK_SIZE, NULL, 2, NULL) != pdPASS) {
    ESP_LOGE(TAG, "FATAL: Failed to create UI loop task");
    return;
  }

  // Start application logic (bridge client)
  ESP_LOGI(TAG, "Starting app...");
  app_entry();
  show_config_durability_diagnostic();

  // Start WiFi (events will trigger mDNS init and bridge connection)
  ESP_LOGI(TAG, "Starting WiFi...");
  wifi_mgr_start();

  ESP_LOGI(TAG, "Initialization complete");
}
