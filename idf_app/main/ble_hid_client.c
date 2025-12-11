/**
 * @file ble_hid_client.c
 * @brief BLE HID client using BlueKnob's proven BLE stack
 */

#include "sdkconfig.h"  // Must be first for CONFIG_* macros
#include "ble_hid_client.h"
#include "ui.h"

#ifdef CONFIG_ROON_KNOB_BLE_HID_ENABLED

#include "esp_hidd_prf_api.h"
#include "hid_dev.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "ble_hid";

#ifndef CONFIG_ROON_KNOB_BLE_DEVICE_NAME
#define CONFIG_ROON_KNOB_BLE_DEVICE_NAME "Roon Knob"
#endif

/* State */
static ble_hid_state_t s_state = BLE_HID_STATE_DISABLED;
static uint16_t s_hid_conn_id = 0;
static ble_hid_state_cb_t s_state_callback = NULL;
static char s_connected_device_name[32] = {0};

/* BLE advertising configuration */
static uint8_t hidd_service_uuid128[] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x03C1,  /* HID Keyboard */
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void set_state(ble_hid_state_t new_state, const char *device_name) {
    if (s_state != new_state) {
        s_state = new_state;
        if (device_name) {
            strncpy(s_connected_device_name, device_name, sizeof(s_connected_device_name) - 1);
        } else {
            s_connected_device_name[0] = '\0';
        }
        if (s_state_callback) {
            s_state_callback(new_state, s_connected_device_name);
        }
    }
}

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param) {
    ESP_LOGI(TAG, "HIDD event: %d", event);
    switch (event) {
        case ESP_HIDD_EVENT_REG_FINISH:
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                ESP_LOGI(TAG, "HID profile registered, setting device name: %s", CONFIG_ROON_KNOB_BLE_DEVICE_NAME);
                esp_ble_gap_set_device_name(CONFIG_ROON_KNOB_BLE_DEVICE_NAME);
                esp_ble_gap_config_adv_data(&hidd_adv_data);
            }
            break;

        case ESP_HIDD_EVENT_BLE_CONNECT:
            ESP_LOGI(TAG, "BLE HID connected, conn_id=%d", param->connect.conn_id);
            s_hid_conn_id = param->connect.conn_id;
            // Set connected state immediately - don't wait for auth (may not happen if already paired)
            set_state(BLE_HID_STATE_CONNECTED, "Device");
            break;

        case ESP_HIDD_EVENT_BLE_DISCONNECT:
            ESP_LOGI(TAG, "BLE HID disconnected");
            set_state(BLE_HID_STATE_ADVERTISING, NULL);
            esp_ble_gap_start_advertising(&hidd_adv_params);
            break;

        default:
            ESP_LOGI(TAG, "Unhandled HIDD event: %d", event);
            break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    ESP_LOGI(TAG, "GAP event: %d", event);
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising data set, starting advertising");
            esp_ble_gap_start_advertising(&hidd_adv_params);
            set_state(BLE_HID_STATE_ADVERTISING, NULL);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising started");
            break;

        case ESP_GAP_BLE_SEC_REQ_EVT:
            ESP_LOGI(TAG, "Security request from device");
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;

        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param->ble_security.auth_cmpl.success) {
                ESP_LOGI(TAG, "BLE pairing successful");
                set_state(BLE_HID_STATE_CONNECTED, "Device");
            } else {
                ESP_LOGW(TAG, "BLE pairing failed: 0x%x",
                         param->ble_security.auth_cmpl.fail_reason);
            }
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG, "Connection params updated");
            break;

        default:
            ESP_LOGD(TAG, "Unhandled GAP event: %d", event);
            break;
    }
}

/**
 * @brief Clear all BLE bonding data from NVS
 * This ensures a fresh start and removes any cached device names
 */
static void clear_ble_bonds(void) {
    ESP_LOGI(TAG, "Clearing BLE bond data from NVS...");

    /* Erase BT config namespace - this is where Bluedroid stores bonds */
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("bt_config.conf", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "BT config cleared");
    }

    /* Also try alternate namespace names used by different ESP-IDF versions */
    err = nvs_open("ble_sec_data", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "BLE security data cleared");
    }
}

bool ble_hid_client_start(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting BLE HID as '%s'...", CONFIG_ROON_KNOB_BLE_DEVICE_NAME);

    /* Note: Don't clear bonds - allows paired devices to auto-reconnect */

    /* Release classic BT memory to save RAM */
    ESP_LOGI(TAG, "Step 1: Releasing classic BT memory...");
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BT memory release failed (may already be released): %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Step 2: Initializing BT controller...");
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Step 3: Enabling BT controller (BLE mode)...");
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Step 4: Initializing Bluedroid...");
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Step 5: Enabling Bluedroid...");
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Step 6: Initializing HID profile...");
    ret = esp_hidd_profile_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID profile init failed: %s", esp_err_to_name(ret));
        return false;
    }

    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    /* Configure security - "just works" pairing (no PIN) */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    ESP_LOGI(TAG, "BLE HID started, advertising as '%s'", CONFIG_ROON_KNOB_BLE_DEVICE_NAME);
    return true;
}

void ble_hid_client_stop(void) {
    ESP_LOGI(TAG, "Stopping BLE HID...");

    esp_ble_gap_stop_advertising();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    set_state(BLE_HID_STATE_DISABLED, NULL);
    ESP_LOGI(TAG, "BLE HID stopped");
}

void ble_hid_client_handle_input(ui_input_event_t event) {
    ESP_LOGI(TAG, "BLE input received: %d (state=%d, conn_id=%d)", event, s_state, s_hid_conn_id);
    // Don't check state - just check if we have a valid connection ID
    // (state tracking seems unreliable, but conn_id gets set on actual connect)
    if (s_hid_conn_id == 0 && s_state == BLE_HID_STATE_DISABLED) {
        ESP_LOGW(TAG, "Input ignored - BLE not started");
        return;
    }

    switch (event) {
        case UI_INPUT_VOL_UP:
            ESP_LOGI(TAG, "Sending volume up");
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_VOLUME_UP, true);
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_VOLUME_UP, false);
            break;

        case UI_INPUT_VOL_DOWN:
            ESP_LOGI(TAG, "Sending volume down");
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_VOLUME_DOWN, true);
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_VOLUME_DOWN, false);
            break;

        case UI_INPUT_PLAY_PAUSE:
            // Send both PLAY and PAUSE - one will be a no-op depending on current state
            ESP_LOGI(TAG, "Sending play then pause");
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_PLAY, true);
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_PLAY, false);
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_PAUSE, true);
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_PAUSE, false);
            break;

        case UI_INPUT_NEXT_TRACK:
            ESP_LOGI(TAG, "Sending next track");
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_SCAN_NEXT_TRK, true);
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_SCAN_NEXT_TRK, false);
            break;

        case UI_INPUT_PREV_TRACK:
            ESP_LOGI(TAG, "Sending previous track");
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_SCAN_PREV_TRK, true);
            esp_hidd_send_consumer_value(s_hid_conn_id, HID_CONSUMER_SCAN_PREV_TRK, false);
            break;

        default:
            break;
    }
}

void ble_hid_client_set_state_callback(ble_hid_state_cb_t callback) {
    s_state_callback = callback;
}

ble_hid_state_t ble_hid_client_get_state(void) {
    return s_state;
}

const char *ble_hid_client_get_connected_device(void) {
    return s_connected_device_name;
}

bool ble_hid_client_available(void) {
    return true;
}

#else /* !CONFIG_ROON_KNOB_BLE_HID_ENABLED */

bool ble_hid_client_start(void) {
    return false;
}

void ble_hid_client_stop(void) {
}

void ble_hid_client_handle_input(ui_input_event_t event) {
    (void)event;
}

void ble_hid_client_set_state_callback(ble_hid_state_cb_t callback) {
    (void)callback;
}

ble_hid_state_t ble_hid_client_get_state(void) {
    return BLE_HID_STATE_DISABLED;
}

const char *ble_hid_client_get_connected_device(void) {
    return "";
}

bool ble_hid_client_available(void) {
    return false;
}

#endif /* CONFIG_ROON_KNOB_BLE_HID_ENABLED */
