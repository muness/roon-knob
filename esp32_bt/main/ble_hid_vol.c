/**
 * @file ble_hid_vol.c
 * @brief BLE HID volume control (dual-mode with Classic BT AVRCP)
 */

#include "ble_hid_vol.h"
#include "esp_hidd_prf_api.h"
#include "hid_dev.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "ble_hid_vol";

static uint16_t s_conn_id = 0;
static bool s_connected = false;
static esp_bd_addr_t s_remote_bda = {0};  // Remote device address
static bool s_directed_adv_pending = false;  // Waiting to start directed advertising
static esp_bd_addr_t s_directed_peer = {0};  // Peer for directed advertising

// Callback when BLE HID connects (to trigger AVRCP connection)
static ble_hid_connect_cb_t s_connect_cb = NULL;

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
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,  // Different MAC than Classic BT
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
        case ESP_HIDD_EVENT_REG_FINISH:
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                ESP_LOGI(TAG, "HID profile registered, starting BLE advertising");
                esp_ble_gap_config_adv_data(&hidd_adv_data);
            }
            break;

        case ESP_HIDD_EVENT_BLE_CONNECT:
            ESP_LOGI(TAG, "BLE HID connected, conn_id=%d", param->connect.conn_id);
            s_conn_id = param->connect.conn_id;
            s_connected = true;
            // Capture remote device address
            memcpy(s_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(TAG, "BLE HID peer: %02x:%02x:%02x:%02x:%02x:%02x",
                     s_remote_bda[0], s_remote_bda[1], s_remote_bda[2],
                     s_remote_bda[3], s_remote_bda[4], s_remote_bda[5]);
            // Notify bt_avrcp to connect AVRCP to this device
            if (s_connect_cb) {
                s_connect_cb(s_remote_bda);
            }
            break;

        case ESP_HIDD_EVENT_BLE_DISCONNECT:
            ESP_LOGI(TAG, "BLE HID disconnected");
            s_connected = false;
            esp_ble_gap_start_advertising(&hidd_adv_params);
            break;

        default:
            ESP_LOGD(TAG, "HIDD event: %d", event);
            break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "BLE advertising data configured, starting advertising");
            esp_ble_gap_start_advertising(&hidd_adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            ESP_LOGI(TAG, "BLE advertising started");
            break;

        case ESP_GAP_BLE_SEC_REQ_EVT:
            ESP_LOGI(TAG, "BLE security request");
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;

        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param->ble_security.auth_cmpl.success) {
                ESP_LOGI(TAG, "BLE pairing successful");
            } else {
                ESP_LOGW(TAG, "BLE pairing failed: 0x%x",
                         param->ble_security.auth_cmpl.fail_reason);
            }
            break;

        default:
            ESP_LOGD(TAG, "GAP event: %d", event);
            break;
    }
}

bool ble_hid_vol_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE HID for volume control...");

    esp_err_t ret = esp_hidd_profile_init();
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

    ESP_LOGI(TAG, "BLE HID initialized");
    return true;
}

void ble_hid_vol_deinit(void)
{
    esp_ble_gap_stop_advertising();
    esp_hidd_profile_deinit();
    s_connected = false;
    s_connect_cb = NULL;
}

void ble_hid_vol_set_connect_callback(ble_hid_connect_cb_t cb)
{
    s_connect_cb = cb;
}

void ble_hid_vol_start_directed_advertising(const uint8_t *peer_addr)
{
    if (s_connected) {
        ESP_LOGI(TAG, "BLE HID already connected, skipping advertising");
        return;
    }

    // Store peer address (for logging)
    memcpy(s_directed_peer, peer_addr, sizeof(esp_bd_addr_t));

    ESP_LOGI(TAG, "Starting general BLE advertising (AVRCP peer: %02x:%02x:%02x:%02x:%02x:%02x)",
             peer_addr[0], peer_addr[1], peer_addr[2],
             peer_addr[3], peer_addr[4], peer_addr[5]);

    // Use general advertising - directed requires the device to be actively scanning
    // which DAPs typically don't do after Classic BT connects
    esp_ble_gap_start_advertising(&hidd_adv_params);
}

bool ble_hid_vol_is_connected(void)
{
    return s_connected;
}

void ble_hid_vol_up(void)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "BLE HID not connected - volume up ignored");
        return;
    }
    ESP_LOGI(TAG, "Sending BLE HID volume up");
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_VOLUME_UP, true);
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_VOLUME_UP, false);
}

void ble_hid_vol_down(void)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "BLE HID not connected - volume down ignored");
        return;
    }
    ESP_LOGI(TAG, "Sending BLE HID volume down");
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_VOLUME_DOWN, true);
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_VOLUME_DOWN, false);
}

void ble_hid_play(void)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "BLE HID not connected - play ignored");
        return;
    }
    ESP_LOGI(TAG, "Sending BLE HID play");
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_PLAY, true);
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_PLAY, false);
}

void ble_hid_pause(void)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "BLE HID not connected - pause ignored");
        return;
    }
    // Send PAUSE only - works on iPhone, test on KANN
    ESP_LOGI(TAG, "Sending BLE HID pause");
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_PAUSE, true);
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_PAUSE, false);
}

void ble_hid_play_pause(void)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "BLE HID not connected - play/pause ignored");
        return;
    }
    // Send PLAY_PAUSE toggle - more universally supported than separate PLAY/PAUSE
    ESP_LOGI(TAG, "Sending BLE HID play/pause toggle");
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_PLAY_PAUSE, true);
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_PLAY_PAUSE, false);
}

void ble_hid_next_track(void)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "BLE HID not connected - next track ignored");
        return;
    }
    ESP_LOGI(TAG, "Sending BLE HID next track");
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_SCAN_NEXT_TRK, true);
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_SCAN_NEXT_TRK, false);
}

void ble_hid_prev_track(void)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "BLE HID not connected - prev track ignored");
        return;
    }
    ESP_LOGI(TAG, "Sending BLE HID prev track");
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_SCAN_PREV_TRK, true);
    esp_hidd_send_consumer_value(s_conn_id, HID_CONSUMER_SCAN_PREV_TRK, false);
}
