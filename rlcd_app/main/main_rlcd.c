#include "app.h"
#include "ble_hid_host_rlcd.h"
#include "bridge_client.h"
#include "captive_portal.h"
#include "platform/platform_input.h"
#include "platform/platform_mdns.h"
#include "platform/platform_task.h"
#include "rlcd_display.h"
#include "rlcd_power_manager.h"
#include "rlcd_ui.h"
#include "wifi_manager.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdatomic.h>

static const char *TAG = "main_rlcd";
static atomic_bool s_mdns_pending = ATOMIC_VAR_INIT(false);
static atomic_bool s_ble_pending = ATOMIC_VAR_INIT(false);
static atomic_bool s_sta_portal_pending = ATOMIC_VAR_INIT(false);

void rk_net_evt_cb(rk_net_evt_t event, const char *ip) {
    switch (event) {
    case RK_NET_EVT_GOT_IP:
        rlcd_ui_set_setup_mode(false);
        rlcd_ui_set_network_status("WiFi connected");
        bridge_client_set_device_ip(ip);
        bridge_client_set_network_ready(true);
        atomic_store(&s_mdns_pending, true);
        atomic_store(&s_ble_pending, true);
        atomic_store(&s_sta_portal_pending, true);
        break;
    case RK_NET_EVT_AP_STARTED:
        rlcd_ui_set_setup_mode(true);
        rlcd_ui_set_network_status("WiFi setup ready");
        bridge_client_set_network_ready(false);
        break;
    case RK_NET_EVT_CONNECTING:
        rlcd_ui_set_network_status("WiFi connecting");
        break;
    case RK_NET_EVT_FAIL:
    case RK_NET_EVT_WRONG_PASSWORD:
    case RK_NET_EVT_NO_AP_FOUND:
    case RK_NET_EVT_AUTH_TIMEOUT:
        rlcd_ui_set_network_status(ip ? ip : "WiFi unavailable");
        bridge_client_set_network_ready(false);
        break;
    default:
        break;
    }
}

static void ui_task(void *arg) {
    (void)arg;
    bool mdns_ready = false;
    bool ble_ready = false;
    for (;;) {
        platform_task_run_pending();
        platform_input_process_events();
        rlcd_ui_process();
        if (atomic_exchange(&s_mdns_pending, false) && !mdns_ready) {
            platform_mdns_init(wifi_mgr_get_hostname());
            mdns_ready = true;
        }
        if (atomic_exchange(&s_ble_pending, false) && !ble_ready) {
            ble_ready = ble_hid_host_rlcd_start();
        }
        if (atomic_exchange(&s_sta_portal_pending, false) && !wifi_mgr_is_ap_mode()) {
            (void)captive_portal_start_sta();
        }
        rlcd_power_manager_poll(
            atomic_load(&s_mdns_pending) || atomic_load(&s_ble_pending) ||
            atomic_load(&s_sta_portal_pending));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void) {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    if (!rlcd_display_init()) {
        ESP_LOGE(TAG, "ST7305 initialization failed");
        return;
    }
    rlcd_ui_init();
    app_controller_init();
    platform_input_init();
    rlcd_power_manager_init();
    /* LVGL's RGB565 image draw path needs appreciably more stack than the
     * text-only screen; keep the artwork renderer on internal RAM. */
    if (xTaskCreate(ui_task, "rlcd_ui", 16384, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "could not start UI task");
        return;
    }
    app_entry();
    wifi_mgr_start();
}
