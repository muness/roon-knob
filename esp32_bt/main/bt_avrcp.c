/**
 * @file bt_avrcp.c
 * @brief Bluetooth dual-mode (Classic AVRCP + BLE HID) implementation
 *
 * This module implements media control over Bluetooth using two complementary profiles:
 *
 * 1. BLE HID (Human Interface Device) - for all controls:
 *    - Play/pause/next/prev via consumer control keys
 *    - Volume up/down via consumer control keys
 *    - Works consistently on both phones and DAPs
 *
 * 2. Classic BT AVRCP (Audio/Video Remote Control Profile) - for metadata only:
 *    - Track metadata (title, artist, album, duration)
 *    - Play position updates
 *    - Play state notifications
 *    - Note: AVRCP passthrough commands are unreliable (DAPs accept but ignore them)
 *
 * Both connections are typically needed for full functionality:
 *    - BLE HID: Required for controls and volume
 *    - AVRCP: Optional but provides metadata/progress when available
 */

#include "bt_avrcp.h"
#include "uart_protocol.h"
#include "ble_hid_vol.h"

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_avrc_api.h"
#include "esp_a2dp_api.h"
#include "esp_gap_ble_api.h"
#include "esp_timer.h"

// Internal BTA headers for SDP record deletion
#include "bta/bta_av_api.h"
#include "bta_av_int.h"
#include "stack/sdp_api.h"
extern tBTA_AV_CB bta_av_cb;

#include <string.h>
#include <inttypes.h>

static const char *TAG = "bt_avrcp";

// Transaction labels for AVRCP commands
#define TL_GET_CAPS         0
#define TL_GET_METADATA     1
#define TL_RN_TRACK_CHANGE  2
#define TL_RN_PLAY_STATUS   3
#define TL_RN_VOLUME        4
#define TL_SET_VOLUME       5
#define TL_RN_PLAY_POS      6

// Polling interval for metadata refresh (when playing)
#define METADATA_POLL_INTERVAL_MS  5000

// State
static bt_state_t s_bt_state = BT_STATE_DISCONNECTED;
static play_state_t s_play_state = PLAY_STATE_UNKNOWN;
static char s_device_name[64] = {0};
static esp_avrc_rn_evt_cap_mask_t s_peer_caps = {0};  // Peer's supported notifications
static uint8_t s_volume = 64;  // Current volume (0-127, default 50%)
static esp_timer_handle_t s_metadata_timer = NULL;

// Forward declarations
static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
static void avrc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);
static void a2dp_sink_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
static void a2dp_sink_data_callback(const uint8_t *data, uint32_t len);
static void set_bt_state(bt_state_t state);
static void set_volume(uint8_t volume);
static void send_passthrough_cmd(uint8_t cmd);
static void metadata_timer_cb(void *arg);
static void on_ble_hid_connect(const uint8_t *remote_bda);

void bt_avrcp_init(void)
{
    ESP_LOGI(TAG, "Initializing Bluetooth dual-mode (Classic + BLE)...");

    // Don't release BLE memory - we need it for BLE HID volume control
    // ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // Initialize BT controller in dual mode (Classic BT + BLE)
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));  // Dual mode

    // Initialize Bluedroid
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Register GAP callback
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_callback));

    // Initialize AVRCP Controller first (must be before A2DP per ESP-IDF docs)
    ESP_ERROR_CHECK(esp_avrc_ct_init());
    ESP_ERROR_CHECK(esp_avrc_ct_register_callback(avrc_ct_callback));

    // Initialize AVRCP Target for receiving volume commands from phone
    ESP_ERROR_CHECK(esp_avrc_tg_init());
    ESP_ERROR_CHECK(esp_avrc_tg_register_callback(avrc_tg_callback));

    // Set up TG to support volume change notifications
    esp_avrc_rn_evt_cap_mask_t tg_evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &tg_evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    ESP_ERROR_CHECK(esp_avrc_tg_set_rn_evt_cap(&tg_evt_set));

    // Initialize A2DP Sink (required for AVRCP to work with phones)
    ESP_ERROR_CHECK(esp_a2d_sink_init());
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2dp_sink_callback));
    esp_a2d_sink_register_data_callback(a2dp_sink_data_callback);

    // Delete A2DP SDP record to prevent heap corruption from audio data
    // Note: Some DAPs (like KANN) need A2DP for AVRCP - they'll only get BLE HID
    vTaskDelay(pdMS_TO_TICKS(100));
    if (bta_av_cb.sdp_a2d_snk_handle != 0) {
        ESP_LOGI(TAG, "Removing A2DP sink SDP record (handle=0x%lx)",
                 (unsigned long)bta_av_cb.sdp_a2d_snk_handle);
        SDP_DeleteRecord(bta_av_cb.sdp_a2d_snk_handle);
        bta_av_cb.sdp_a2d_snk_handle = 0;
    }

    // Set device name for Classic BT (AVRCP metadata)
    esp_bt_gap_set_device_name("Knob info");

    // Set up BLE with different MAC address so DAP sees it as separate device
    esp_bd_addr_t ble_addr;
    const uint8_t *base_addr = esp_bt_dev_get_address();
    memcpy(ble_addr, base_addr, 6);
    ble_addr[0] = (ble_addr[0] | 0xC0);  // Set two MSBs for random static address
    ble_addr[5] ^= 0x01;  // Flip LSB to differentiate from Classic BT
    esp_ble_gap_set_rand_addr(ble_addr);
    ESP_LOGI(TAG, "BLE random address: %02x:%02x:%02x:%02x:%02x:%02x",
             ble_addr[0], ble_addr[1], ble_addr[2],
             ble_addr[3], ble_addr[4], ble_addr[5]);

    // Initialize BLE HID for volume control (dual-mode)
    if (!ble_hid_vol_init()) {
        ESP_LOGW(TAG, "BLE HID init failed - volume via BLE won't work");
    }
    // Register callback for when BLE HID connects
    ble_hid_vol_set_connect_callback(on_ble_hid_connect);

    // Set BLE device name (BLE HID for controls)
    esp_ble_gap_set_device_name("Knob control");

    // Create metadata polling timer (started when connected)
    esp_timer_create_args_t timer_args = {
        .callback = metadata_timer_cb,
        .name = "metadata_poll",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_metadata_timer));

    // Don't enter Classic BT discoverable mode yet - wait for BLE HID to connect first
    // This ensures BLE HID works before we advertise AVRCP
    ESP_LOGI(TAG, "BLE HID advertising, Classic BT hidden until BLE connects...");
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    set_bt_state(BT_STATE_DISCONNECTED);

    ESP_LOGI(TAG, "Bluetooth dual-mode initialized (AVRCP + BLE HID)");
}

void bt_avrcp_deinit(void)
{
    // Stop and delete timer
    if (s_metadata_timer) {
        esp_timer_stop(s_metadata_timer);
        esp_timer_delete(s_metadata_timer);
        s_metadata_timer = NULL;
    }

    ble_hid_vol_deinit();
    esp_a2d_sink_deinit();
    esp_avrc_tg_deinit();
    esp_avrc_ct_deinit();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    set_bt_state(BT_STATE_DISCONNECTED);
}

bt_state_t bt_avrcp_get_state(void)
{
    return s_bt_state;
}

play_state_t bt_avrcp_get_play_state(void)
{
    return s_play_state;
}

const char *bt_avrcp_get_device_name(void)
{
    return s_device_name[0] ? s_device_name : NULL;
}

void bt_avrcp_enter_pairing_mode(void)
{
    ESP_LOGI(TAG, "Entering pairing mode");
    // TODO: Disconnect if connected, clear bonds
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    set_bt_state(BT_STATE_DISCOVERABLE);
}

void bt_avrcp_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting...");
    // TODO: Implement disconnect
    set_bt_state(BT_STATE_DISCONNECTED);
}

void bt_avrcp_connect(void)
{
    ESP_LOGI(TAG, "Connecting to last device...");
    // TODO: Implement reconnect to last bonded device
    set_bt_state(BT_STATE_CONNECTING);
}

void bt_avrcp_play(void)
{
    // Use BLE HID for controls - works consistently on phones and DAPs
    ESP_LOGI(TAG, "Play via BLE HID");
    ble_hid_play();
}

void bt_avrcp_pause(void)
{
    ESP_LOGI(TAG, "Pause via BLE HID");
    ble_hid_pause();
}

void bt_avrcp_play_pause(void)
{
    ESP_LOGI(TAG, "Play/Pause toggle via BLE HID");
    ble_hid_play_pause();
}

void bt_avrcp_next(void)
{
    ESP_LOGI(TAG, "Next via BLE HID");
    ble_hid_next_track();
}

void bt_avrcp_prev(void)
{
    ESP_LOGI(TAG, "Prev via BLE HID");
    ble_hid_prev_track();
}

void bt_avrcp_vol_up(void)
{
    ESP_LOGI(TAG, "Volume Up via BLE HID");
    ble_hid_vol_up();
}

void bt_avrcp_vol_down(void)
{
    ESP_LOGI(TAG, "Volume Down via BLE HID");
    ble_hid_vol_down();
}

void bt_avrcp_set_volume(uint8_t volume)
{
    if (volume > 127) volume = 127;

    if (s_bt_state != BT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Cannot set volume - not connected");
        return;
    }

    ESP_LOGI(TAG, "AVRCP: Set absolute volume to %d", volume);
    esp_avrc_ct_send_set_absolute_volume_cmd(TL_SET_VOLUME, volume);

    // Optimistically update local volume (phone may not respond to set_volume_rsp)
    // If phone does respond, set_volume() will be called again (no-op if same value)
    set_volume(volume);
}

uint8_t bt_avrcp_get_volume(void)
{
    return s_volume;
}

// --- Private functions ---

// Request metadata refresh (includes duration via PLAYING_TIME)
static void request_metadata(void)
{
    if (s_bt_state != BT_STATE_CONNECTED) {
        return;
    }
    ESP_LOGD(TAG, "Requesting metadata refresh");
    esp_avrc_ct_send_metadata_cmd(TL_GET_METADATA,
        ESP_AVRC_MD_ATTR_TITLE |
        ESP_AVRC_MD_ATTR_ARTIST |
        ESP_AVRC_MD_ATTR_ALBUM |
        ESP_AVRC_MD_ATTR_PLAYING_TIME);
}

// Timer callback for periodic metadata polling
static void metadata_timer_cb(void *arg)
{
    (void)arg;
    // Poll metadata when connected (duration useful even when paused)
    request_metadata();
}

// Start/stop metadata polling timer
static void start_metadata_timer(void)
{
    if (s_metadata_timer && s_bt_state == BT_STATE_CONNECTED) {
        esp_timer_start_periodic(s_metadata_timer, METADATA_POLL_INTERVAL_MS * 1000);
        ESP_LOGI(TAG, "Started metadata polling timer (%d ms)", METADATA_POLL_INTERVAL_MS);
    }
}

static void stop_metadata_timer(void)
{
    if (s_metadata_timer) {
        esp_timer_stop(s_metadata_timer);
        ESP_LOGI(TAG, "Stopped metadata polling timer");
    }
}

static void set_bt_state(bt_state_t state)
{
    if (s_bt_state != state) {
        s_bt_state = state;
        ESP_LOGI(TAG, "BT state changed: %d", state);
        // Notify S3 via UART
        uart_protocol_send_bt_state(state);
    }
}

static void set_play_state(play_state_t state)
{
    if (s_play_state != state) {
        s_play_state = state;
        ESP_LOGI(TAG, "Play state changed: %d", state);
        // Notify S3 via UART
        uart_protocol_send_play_status(state);
    }
}

static void set_volume(uint8_t volume)
{
    if (s_volume != volume) {
        s_volume = volume;
        ESP_LOGI(TAG, "Volume changed: %d (%.0f%%)", volume, volume * 100.0 / 127.0);
        // Notify S3 via UART
        uart_protocol_send_volume(volume);
    }
}

static void send_passthrough_cmd(uint8_t cmd)
{
    static uint8_t s_tl = 0;  // Transaction label

    if (s_bt_state != BT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Cannot send command - not connected");
        return;
    }

    ESP_LOGI(TAG, "Sending passthrough cmd 0x%02x (tl=%d)", cmd, s_tl);

    // Press
    esp_err_t err = esp_avrc_ct_send_passthrough_cmd(s_tl, cmd, ESP_AVRC_PT_CMD_STATE_PRESSED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Passthrough PRESS failed: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    // Release
    err = esp_avrc_ct_send_passthrough_cmd(s_tl, cmd, ESP_AVRC_PT_CMD_STATE_RELEASED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Passthrough RELEASE failed: %s", esp_err_to_name(err));
    }

    s_tl = (s_tl + 1) & 0x0F;  // Wrap at 16
}

// A2DP Sink data callback - just discard audio data
static void a2dp_sink_data_callback(const uint8_t *data, uint32_t len)
{
    // Discard audio data - we only care about AVRCP control
    (void)data;
    (void)len;
}

// A2DP Sink callback - just log, don't disconnect (would kill ACL)
// The SDP record deletion should prevent A2DP discovery
// If A2DP connects anyway (cached service), just ignore the audio data
static void a2dp_sink_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
                ESP_LOGW(TAG, "A2DP connection attempt (ignoring, SDP removed)");
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGW(TAG, "A2DP Sink connected (will ignore audio data)");
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "A2DP Sink disconnected");
            }
            break;
        default:
            ESP_LOGD(TAG, "A2DP Sink event: %d", event);
            break;
    }
}

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                strncpy(s_device_name, (char *)param->auth_cmpl.device_name, sizeof(s_device_name) - 1);
                uart_protocol_send_device_name(s_device_name);
            } else {
                ESP_LOGW(TAG, "Authentication failed: %d", param->auth_cmpl.stat);
            }
            break;

        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
            ESP_LOGI(TAG, "ACL connected");
            break;

        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
            ESP_LOGI(TAG, "ACL disconnected");
            set_bt_state(BT_STATE_DISCONNECTED);
            s_device_name[0] = '\0';
            // Re-enter discoverable mode
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            set_bt_state(BT_STATE_DISCOVERABLE);
            break;

        default:
            ESP_LOGD(TAG, "GAP event: %d", event);
            break;
    }
}

static void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
            if (param->conn_stat.connected) {
                ESP_LOGI(TAG, "AVRCP connected to %02x:%02x:%02x:%02x:%02x:%02x",
                         param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1],
                         param->conn_stat.remote_bda[2], param->conn_stat.remote_bda[3],
                         param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);
                set_bt_state(BT_STATE_CONNECTED);

                // Start metadata polling timer
                start_metadata_timer();

                // Start directed BLE advertising to this specific device
                ESP_LOGI(TAG, "Starting directed BLE advertising to AVRCP peer");
                ble_hid_vol_start_directed_advertising(param->conn_stat.remote_bda);

                // First, get peer's supported notification capabilities
                // We'll register for notifications in the response handler
                esp_avrc_ct_send_get_rn_capabilities_cmd(TL_GET_CAPS);
            } else {
                ESP_LOGI(TAG, "AVRCP disconnected");
                stop_metadata_timer();
                set_bt_state(BT_STATE_DISCONNECTED);
                s_peer_caps.bits = 0;  // Clear cached capabilities
            }
            break;

        case ESP_AVRC_CT_METADATA_RSP_EVT: {
            uint8_t attr_id = param->meta_rsp.attr_id;
            char *text = (char *)param->meta_rsp.attr_text;

            ESP_LOGI(TAG, "Metadata[%d]: %s", attr_id, text ? text : "(null)");

            if (text) {
                // Map ESP-IDF attr_id (bitmask values) to our protocol meta_type (sequential)
                // ESP-IDF: TITLE=0x1, ARTIST=0x2, ALBUM=0x4, PLAYING_TIME=0x40
                // Protocol: TITLE=1, ARTIST=2, ALBUM=3, DURATION=4
                uint8_t meta_type = 0;
                switch (attr_id) {
                    case 0x01:  // ESP_AVRC_MD_ATTR_TITLE
                        meta_type = 0x01;  // META_TITLE
                        break;
                    case 0x02:  // ESP_AVRC_MD_ATTR_ARTIST
                        meta_type = 0x02;  // META_ARTIST
                        break;
                    case 0x04:  // ESP_AVRC_MD_ATTR_ALBUM
                        meta_type = 0x03;  // META_ALBUM
                        break;
                    case 0x40: { // ESP_AVRC_MD_ATTR_PLAYING_TIME
                        meta_type = 0x04;  // META_DURATION
                        // ESP-IDF sometimes doesn't null-terminate properly
                        // Find first non-digit and truncate there
                        char *p = text;
                        while (*p >= '0' && *p <= '9') p++;
                        *p = '\0';
                        ESP_LOGI(TAG, "Track duration: %s ms", text);
                        break;
                    }
                    default:
                        ESP_LOGW(TAG, "Unknown metadata attr_id: 0x%02X", attr_id);
                        break;
                }
                if (meta_type) {
                    uart_protocol_send_metadata(meta_type, text);
                }
            }
            break;
        }

        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
            // Got peer's supported notification capabilities
            ESP_LOGI(TAG, "Peer capabilities: count=%d, bitmask=0x%x",
                     param->get_rn_caps_rsp.cap_count, (unsigned)param->get_rn_caps_rsp.evt_set.bits);
            s_peer_caps.bits = param->get_rn_caps_rsp.evt_set.bits;

            // Request current metadata
            esp_avrc_ct_send_metadata_cmd(TL_GET_METADATA,
                ESP_AVRC_MD_ATTR_TITLE |
                ESP_AVRC_MD_ATTR_ARTIST |
                ESP_AVRC_MD_ATTR_ALBUM |
                ESP_AVRC_MD_ATTR_PLAYING_TIME);

            // Register for track change notifications if supported
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_peer_caps,
                                                    ESP_AVRC_RN_TRACK_CHANGE)) {
                ESP_LOGI(TAG, "Registering for track change notifications");
                esp_avrc_ct_send_register_notification_cmd(TL_RN_TRACK_CHANGE,
                                                           ESP_AVRC_RN_TRACK_CHANGE, 0);
            }

            // Register for playback status notifications if supported
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_peer_caps,
                                                    ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {
                ESP_LOGI(TAG, "Registering for play status notifications");
                esp_avrc_ct_send_register_notification_cmd(TL_RN_PLAY_STATUS,
                                                           ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
            }

            // Register for volume change notifications if supported
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_peer_caps,
                                                    ESP_AVRC_RN_VOLUME_CHANGE)) {
                ESP_LOGI(TAG, "Registering for volume change notifications");
                esp_avrc_ct_send_register_notification_cmd(TL_RN_VOLUME,
                                                           ESP_AVRC_RN_VOLUME_CHANGE, 0);
            }

            // Register for play position change notifications if supported
            // Interval is in seconds - request every 1 second
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_peer_caps,
                                                    ESP_AVRC_RN_PLAY_POS_CHANGED)) {
                ESP_LOGI(TAG, "Registering for play position notifications (1s interval)");
                esp_avrc_ct_send_register_notification_cmd(TL_RN_PLAY_POS,
                                                           ESP_AVRC_RN_PLAY_POS_CHANGED, 1);
            }
            break;
        }

        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
            uint8_t event_id = param->change_ntf.event_id;

            if (event_id == ESP_AVRC_RN_TRACK_CHANGE) {
                ESP_LOGI(TAG, "Track changed - requesting metadata");
                esp_avrc_ct_send_metadata_cmd(TL_GET_METADATA,
                    ESP_AVRC_MD_ATTR_TITLE |
                    ESP_AVRC_MD_ATTR_ARTIST |
                    ESP_AVRC_MD_ATTR_ALBUM |
                    ESP_AVRC_MD_ATTR_PLAYING_TIME);
                // Re-register for notification
                esp_avrc_ct_send_register_notification_cmd(TL_RN_TRACK_CHANGE,
                                                           ESP_AVRC_RN_TRACK_CHANGE, 0);
            } else if (event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
                uint8_t play_status = param->change_ntf.event_parameter.playback;
                ESP_LOGI(TAG, "Play status changed: %d", play_status);

                play_state_t state = PLAY_STATE_UNKNOWN;
                switch (play_status) {
                    case ESP_AVRC_PLAYBACK_STOPPED:
                        state = PLAY_STATE_STOPPED;
                        break;
                    case ESP_AVRC_PLAYBACK_PLAYING:
                        state = PLAY_STATE_PLAYING;
                        break;
                    case ESP_AVRC_PLAYBACK_PAUSED:
                        state = PLAY_STATE_PAUSED;
                        break;
                    default:
                        break;
                }
                set_play_state(state);

                // Re-register for notification
                esp_avrc_ct_send_register_notification_cmd(TL_RN_PLAY_STATUS,
                                                           ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
            } else if (event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
                uint8_t volume = param->change_ntf.event_parameter.volume;
                ESP_LOGI(TAG, "Volume changed (notification): %d", volume);
                set_volume(volume);
                // Re-register for notification
                esp_avrc_ct_send_register_notification_cmd(TL_RN_VOLUME,
                                                           ESP_AVRC_RN_VOLUME_CHANGE, 0);
            } else if (event_id == ESP_AVRC_RN_PLAY_POS_CHANGED) {
                uint32_t pos_ms = param->change_ntf.event_parameter.play_pos;
                ESP_LOGI(TAG, "Play position: %lu ms", (unsigned long)pos_ms);
                // Send position to S3 via UART
                uart_protocol_send_position(pos_ms);
                // Re-register for notification (1 second interval)
                esp_avrc_ct_send_register_notification_cmd(TL_RN_PLAY_POS,
                                                           ESP_AVRC_RN_PLAY_POS_CHANGED, 1);
            }
            break;
        }

        case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
            uint8_t volume = param->set_volume_rsp.volume;
            ESP_LOGI(TAG, "Set volume response: %d", volume);
            set_volume(volume);
            break;
        }

        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
            ESP_LOGI(TAG, "Passthrough response: tl=%d, key=0x%02x, state=%d, rsp=%d",
                     param->psth_rsp.tl, param->psth_rsp.key_code,
                     param->psth_rsp.key_state, param->psth_rsp.rsp_code);
            break;

        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
            ESP_LOGI(TAG, "Remote features: 0x%" PRIx32, param->rmt_feats.feat_mask);
            break;

        default:
            ESP_LOGD(TAG, "AVRC CT event: %d", event);
            break;
    }
}

// Callback when BLE HID connects - now enable Classic BT for AVRCP
static void on_ble_hid_connect(const uint8_t *remote_bda)
{
    ESP_LOGI(TAG, "BLE HID connected to %02x:%02x:%02x:%02x:%02x:%02x",
             remote_bda[0], remote_bda[1], remote_bda[2],
             remote_bda[3], remote_bda[4], remote_bda[5]);

    // If AVRCP is already connected, nothing to do
    if (s_bt_state == BT_STATE_CONNECTED) {
        ESP_LOGI(TAG, "AVRCP already connected");
        return;
    }

    // Now enable Classic BT discoverable mode for AVRCP
    // A2DP SDP was already deleted at startup to prevent heap corruption
    ESP_LOGI(TAG, "Enabling Classic BT 'Knob info' for AVRCP (A2DP disabled)");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    set_bt_state(BT_STATE_DISCOVERABLE);
}

// AVRCP Target callback - receives volume commands FROM the phone
static void avrc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    ESP_LOGI(TAG, "AVRC TG event: %d", event);  // Log ALL events

    switch (event) {
        case ESP_AVRC_TG_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "AVRCP TG %s", param->conn_stat.connected ? "connected" : "disconnected");
            break;

        case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
            // Phone is setting our volume (as if we were speakers)
            uint8_t volume = param->set_abs_vol.volume;
            ESP_LOGI(TAG, "Phone set volume to: %d (%.0f%%)", volume, volume * 100.0 / 127.0);
            set_volume(volume);
            break;
        }

        case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
            // Phone is registering for notifications from us
            ESP_LOGI(TAG, "Phone registering for notification event_id=%d", param->reg_ntf.event_id);
            if (param->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
                ESP_LOGI(TAG, "Phone registered for volume notifications - sending current vol %d", s_volume);
                // Send current volume as response to phone
                esp_avrc_rn_param_t rn_param = {0};
                rn_param.volume = s_volume;
                esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);
                // Also send current volume to S3
                uart_protocol_send_volume(s_volume);
            }
            break;

        case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
            ESP_LOGI(TAG, "TG Remote features: 0x%" PRIx32 ", CT flag: 0x%x",
                     param->rmt_feats.feat_mask, param->rmt_feats.ct_feat_flag);
            break;

        case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
            ESP_LOGI(TAG, "TG Passthrough cmd: key=0x%02x state=%d",
                     param->psth_cmd.key_code, param->psth_cmd.key_state);
            break;

        default:
            ESP_LOGI(TAG, "AVRC TG unhandled event: %d", event);
            break;
    }
}
