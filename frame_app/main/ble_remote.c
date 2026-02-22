// BLE HID Host for hiphi frame — pairs with media remotes
// Uses NimBLE + esp_hid component (HOGP profile)

#include "ble_remote.h"
#include "eink_ui.h"
#include "bridge_client.h"
#include "ui.h"

#include <esp_hidh.h>
#include <esp_hid_common.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>

static const char *TAG = "ble_remote";

// NVS keys for bonded device
#define NVS_NAMESPACE "ble_remote"
#define NVS_KEY_BDA   "bonded_bda"
#define NVS_KEY_ATYPE "bonded_atype"
#define NVS_KEY_NAME  "bonded_name"

// Max reconnect attempts before giving up (retry via web UI or reboot)
#define MAX_RECONNECT_ATTEMPTS 20

// ── State (protected by s_mutex) ─────────────────────────────────────────────

static SemaphoreHandle_t s_mutex = NULL;

static uint8_t s_own_addr_type;
static esp_hidh_dev_t *s_connected_dev = NULL;
static volatile bool s_connected = false;
static volatile bool s_scanning = false;
static char s_device_name[64] = "";

// Scan results
static ble_remote_device_t s_scan_results[BLE_REMOTE_MAX_RESULTS];
static int s_scan_count = 0;
static SemaphoreHandle_t s_scan_sem = NULL;

// Bonded device (loaded from NVS)
static bool s_has_bonded = false;
static uint8_t s_bonded_bda[6];
static uint8_t s_bonded_addr_type;
static char s_bonded_name[64] = "";

// Pending connect addr type (set before esp_hidh_dev_open, used in callback)
static uint8_t s_pending_addr_type = 0;

// Unpair-in-progress flag: when true, CLOSE callback skips dev_free (unpair owns it)
static volatile bool s_unpair_pending = false;

// System-wide GAP event listener (receives events from esp_hidh's connections too)
static struct ble_gap_event_listener s_gap_listener;

#define BLE_LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define BLE_UNLOCK() xSemaphoreGive(s_mutex)

// ── NVS helpers ──────────────────────────────────────────────────────────────
// Caller must hold s_mutex

static void save_bonded_device(const uint8_t *bda, uint8_t addr_type, const char *name) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_BDA, bda, 6);
        nvs_set_u8(h, NVS_KEY_ATYPE, addr_type);
        nvs_set_str(h, NVS_KEY_NAME, name ? name : "");
        nvs_commit(h);
        nvs_close(h);
        memcpy(s_bonded_bda, bda, 6);
        s_bonded_addr_type = addr_type;
        snprintf(s_bonded_name, sizeof(s_bonded_name), "%s", name ? name : "");
        s_has_bonded = true;
        ESP_LOGI(TAG, "Saved bonded device: %s [%02x:%02x:%02x:%02x:%02x:%02x]",
                 s_bonded_name, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    }
}

static void load_bonded_device(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    size_t len = 6;
    if (nvs_get_blob(h, NVS_KEY_BDA, s_bonded_bda, &len) == ESP_OK && len == 6) {
        nvs_get_u8(h, NVS_KEY_ATYPE, &s_bonded_addr_type);
        len = sizeof(s_bonded_name);
        nvs_get_str(h, NVS_KEY_NAME, s_bonded_name, &len);
        s_has_bonded = true;
        ESP_LOGI(TAG, "Loaded bonded device: %s [%02x:%02x:%02x:%02x:%02x:%02x]",
                 s_bonded_name,
                 s_bonded_bda[0], s_bonded_bda[1], s_bonded_bda[2],
                 s_bonded_bda[3], s_bonded_bda[4], s_bonded_bda[5]);
    }
    nvs_close(h);
}

static void clear_bonded_device(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_BDA);
        nvs_erase_key(h, NVS_KEY_ATYPE);
        nvs_erase_key(h, NVS_KEY_NAME);
        nvs_commit(h);
        nvs_close(h);
    }
    s_has_bonded = false;
    memset(s_bonded_bda, 0, 6);
    s_bonded_name[0] = '\0';
}

// ── HID Consumer Control mapping ────────────────────────────────────────────

static void handle_consumer_control(const uint8_t *data, uint16_t len) {
    if (len < 2) return;

    uint16_t usage = data[0] | (data[1] << 8);
    if (usage == 0x0000) return;  // Key release

    ESP_LOGI(TAG, "Consumer Control: 0x%04x", usage);

    switch (usage) {
    case 0x00CD:  // Play/Pause
        bridge_client_handle_input(UI_INPUT_PLAY_PAUSE);
        break;
    case 0x00B5:  // Next Track
        bridge_client_handle_input(UI_INPUT_NEXT_TRACK);
        break;
    case 0x00B6:  // Previous Track
        bridge_client_handle_input(UI_INPUT_PREV_TRACK);
        break;
    case 0x00E9:  // Volume Up
        bridge_client_handle_input(UI_INPUT_VOL_UP);
        break;
    case 0x00EA:  // Volume Down
        bridge_client_handle_input(UI_INPUT_VOL_DOWN);
        break;
    case 0x00E2:  // Mute — no bridge mute concept, ignore
        ESP_LOGD(TAG, "Mute key ignored (no bridge mute support)");
        break;
    default:
        ESP_LOGD(TAG, "Unhandled consumer control: 0x%04x", usage);
        break;
    }
}

// ── HIDH event callback ─────────────────────────────────────────────────────

static void hidh_callback(void *handler_args, esp_event_base_t base,
                           int32_t id, void *event_data) {
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDH_OPEN_EVENT:
        BLE_LOCK();
        if (param->open.status == ESP_OK) {
            s_connected_dev = param->open.dev;
            s_connected = true;
            const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
            const char *name = esp_hidh_dev_name_get(param->open.dev);
            snprintf(s_device_name, sizeof(s_device_name), "%s", name ? name : "BLE Remote");
            ESP_LOGI(TAG, "Connected to: %s", s_device_name);

            // Save as bonded device
            if (bda) {
                save_bonded_device(bda, s_pending_addr_type, s_device_name);
            }
            BLE_UNLOCK();
            eink_ui_set_ble_status(true);
        } else {
            BLE_UNLOCK();
            ESP_LOGW(TAG, "HID open failed: %d", param->open.status);
        }
        break;

    case ESP_HIDH_INPUT_EVENT:
        if (param->input.usage == ESP_HID_USAGE_CCONTROL) {
            handle_consumer_control(param->input.data, param->input.length);
        }
        break;

    case ESP_HIDH_BATTERY_EVENT:
        ESP_LOGI(TAG, "BLE remote battery: %d%%", param->battery.level);
        break;

    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGI(TAG, "BLE remote disconnected (reason: %d)", param->close.reason);
        BLE_LOCK();
        // Only free the device if unpair isn't managing the lifecycle
        if (!s_unpair_pending && param->close.dev) {
            esp_hidh_dev_free(param->close.dev);
        }
        s_connected_dev = NULL;
        s_connected = false;
        s_device_name[0] = '\0';
        BLE_UNLOCK();
        eink_ui_set_ble_status(false);
        break;

    default:
        break;
    }
}

// ── BLE GAP scan callback ───────────────────────────────────────────────────

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        // Check if this device advertises HID Service (UUID 0x1812)
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data) != 0) {
            break;
        }

        bool has_hid = false;
        for (int i = 0; i < fields.num_uuids16; i++) {
            if (ble_uuid_u16(&fields.uuids16[i].u) == 0x1812) {
                has_hid = true;
                break;
            }
        }
        if (!has_hid) break;

        BLE_LOCK();
        if (s_scan_count >= BLE_REMOTE_MAX_RESULTS) {
            BLE_UNLOCK();
            break;
        }

        // Check for duplicate BDA
        bool found = false;
        for (int i = 0; i < s_scan_count; i++) {
            if (memcmp(s_scan_results[i].bda, event->disc.addr.val, 6) == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            BLE_UNLOCK();
            break;
        }

        ble_remote_device_t *r = &s_scan_results[s_scan_count];
        memcpy(r->bda, event->disc.addr.val, 6);
        r->addr_type = event->disc.addr.type;

        if (fields.name && fields.name_len > 0) {
            int len = fields.name_len < 63 ? fields.name_len : 63;
            memcpy(r->name, fields.name, len);
            r->name[len] = '\0';
        } else {
            snprintf(r->name, sizeof(r->name), "%02X:%02X:%02X:%02X:%02X:%02X",
                     r->bda[0], r->bda[1], r->bda[2],
                     r->bda[3], r->bda[4], r->bda[5]);
        }

        ESP_LOGI(TAG, "Found HID device: %s [%02x:%02x:%02x:%02x:%02x:%02x]",
                 r->name, r->bda[0], r->bda[1], r->bda[2],
                 r->bda[3], r->bda[4], r->bda[5]);
        s_scan_count++;
        BLE_UNLOCK();
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Scan complete, found %d HID devices", s_scan_count);
        s_scanning = false;
        if (s_scan_sem) xSemaphoreGive(s_scan_sem);
        break;

    // ENC_CHANGE and REPEAT_PAIRING handled by system-wide listener
    // (s_gap_listener_cb) which covers esp_hidh's connections too.

    default:
        break;
    }
    return 0;
}

// ── System-wide GAP event listener ──────────────────────────────────────────
// Catches events from esp_hidh's internal connections (which use their own
// GAP callback). Needed because nimble_hidh.c has no security handling.

static int ble_gap_listener_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Connection up (handle=%d), initiating security...",
                     event->connect.conn_handle);
            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0) {
                ESP_LOGW(TAG, "Security initiate failed: %d", rc);
            }
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: status=%d handle=%d",
                 event->enc_change.status, event->enc_change.conn_handle);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // Delete old bond and retry
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        ESP_LOGI(TAG, "Repeat pairing — deleted old bond, retrying");
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        break;
    }
    return 0;
}

// ── NimBLE host task and sync ───────────────────────────────────────────────

static void ble_on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE host synced, addr_type=%d", s_own_addr_type);
}

static void ble_on_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset: reason=%d", reason);
}

static void nimble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ── Reconnect task ──────────────────────────────────────────────────────────

static void reconnect_task(void *arg) {
    // Wait for BLE stack to sync
    vTaskDelay(pdMS_TO_TICKS(2000));

    int attempts = 0;
    while (s_has_bonded && !s_connected && attempts < MAX_RECONNECT_ATTEMPTS) {
        attempts++;
        ESP_LOGI(TAG, "Reconnect attempt %d/%d to %s...",
                 attempts, MAX_RECONNECT_ATTEMPTS, s_bonded_name);
        BLE_LOCK();
        s_pending_addr_type = s_bonded_addr_type;
        BLE_UNLOCK();
        esp_hidh_dev_open(s_bonded_bda, ESP_HID_TRANSPORT_BLE, s_bonded_addr_type);
        if (s_connected) break;
        ESP_LOGW(TAG, "Reconnect failed, retrying in 15s...");
        vTaskDelay(pdMS_TO_TICKS(15000));
    }

    if (!s_connected && attempts >= MAX_RECONNECT_ATTEMPTS) {
        ESP_LOGW(TAG, "Reconnect gave up after %d attempts", MAX_RECONNECT_ATTEMPTS);
    }
    ESP_LOGI(TAG, "Reconnect task done (connected=%d)", s_connected);
    vTaskDelete(NULL);
}

// ── Scan task (runs in background) ──────────────────────────────────────────

static void scan_task(void *arg) {
    BLE_LOCK();
    s_scan_count = 0;
    s_scanning = true;
    BLE_UNLOCK();

    struct ble_gap_disc_params disc_params = {
        .filter_duplicates = 1,
        .passive = 0,
        .itvl = 0x0050,
        .window = 0x0030,
        .filter_policy = 0,
        .limited = 0,
    };

    ESP_LOGI(TAG, "Starting BLE HID scan (5s)...");
    int rc = ble_gap_disc(s_own_addr_type, 5000, &disc_params, ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        s_scanning = false;
    } else {
        // Wait for DISC_COMPLETE event
        if (s_scan_sem) {
            xSemaphoreTake(s_scan_sem, pdMS_TO_TICKS(8000));
        }
    }

    s_scanning = false;  // Ensure flag is cleared even on timeout
    vTaskDelete(NULL);
}

// ── Pair task (runs in background) ──────────────────────────────────────────

static void pair_task(void *arg) {
    int index = (int)(intptr_t)arg;

    BLE_LOCK();
    if (index < 0 || index >= s_scan_count) {
        BLE_UNLOCK();
        ESP_LOGE(TAG, "Invalid pair index: %d", index);
        vTaskDelete(NULL);
        return;
    }

    ble_remote_device_t dev = s_scan_results[index];  // Copy under lock
    s_pending_addr_type = dev.addr_type;
    BLE_UNLOCK();

    ESP_LOGI(TAG, "Pairing with: %s", dev.name);
    // esp_hidh_dev_open is blocking — does GATT discovery, subscribes to notifications
    esp_hidh_dev_open(dev.bda, ESP_HID_TRANSPORT_BLE, dev.addr_type);

    vTaskDelete(NULL);
}

// ── Public API ──────────────────────────────────────────────────────────────

void ble_remote_init(void) {
    ESP_LOGI(TAG, "Initializing BLE remote...");

    s_mutex = xSemaphoreCreateMutex();
    s_scan_sem = xSemaphoreCreateBinary();

    // Initialize NimBLE stack (handles controller + host for ESP32-S3)
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return;
    }

    // Configure NimBLE security for bonding
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Initialize HID host
    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    esp_err_t hidh_ret = esp_hidh_init(&hidh_cfg);
    if (hidh_ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidh_init failed: %s", esp_err_to_name(hidh_ret));
        return;
    }

    // Start NimBLE host task
    nimble_port_freertos_init(nimble_host_task);

    // Register system-wide GAP listener to handle security for esp_hidh
    // connections (nimble_hidh.c has no security handling of its own)
    ble_gap_event_listener_register(&s_gap_listener, ble_gap_listener_cb, NULL);

    vTaskDelay(pdMS_TO_TICKS(500));

    // Load bonded device from NVS
    load_bonded_device();

    // If we have a bonded device, start reconnect attempts
    if (s_has_bonded) {
        xTaskCreate(reconnect_task, "ble_reconn", 4096, NULL, 2, NULL);
    }

    ESP_LOGI(TAG, "BLE remote initialized");
}

void ble_remote_scan_start(void) {
    if (s_scanning) {
        ESP_LOGW(TAG, "Scan already in progress");
        return;
    }
    // Set scanning early to prevent double-start race
    s_scanning = true;
    xTaskCreate(scan_task, "ble_scan", 4096, NULL, 3, NULL);
}

bool ble_remote_is_scanning(void) {
    return s_scanning;
}

int ble_remote_get_scan_results(ble_remote_device_t *out, int max) {
    BLE_LOCK();
    int count = s_scan_count < max ? s_scan_count : max;
    if (count > 0) {
        memcpy(out, s_scan_results, count * sizeof(ble_remote_device_t));
    }
    BLE_UNLOCK();
    return count;
}

void ble_remote_pair(int index) {
    xTaskCreate(pair_task, "ble_pair", 4096, (void *)(intptr_t)index, 3, NULL);
}

void ble_remote_unpair(void) {
    BLE_LOCK();
    esp_hidh_dev_t *dev = s_connected_dev;
    bool was_connected = s_connected && dev;
    if (was_connected) {
        s_unpair_pending = true;  // Tell CLOSE callback not to free
    }
    s_connected = false;
    s_connected_dev = NULL;
    s_device_name[0] = '\0';
    clear_bonded_device();
    BLE_UNLOCK();

    if (was_connected) {
        esp_hidh_dev_close(dev);
        // Give CLOSE event time to fire, then free
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_hidh_dev_free(dev);
        s_unpair_pending = false;
    }

    eink_ui_set_ble_status(false);
    ESP_LOGI(TAG, "Unpaired BLE remote");
}

bool ble_remote_is_connected(void) {
    return s_connected;
}

void ble_remote_device_name(char *out, size_t len) {
    if (!out || len == 0) return;
    BLE_LOCK();
    if (s_device_name[0]) {
        strncpy(out, s_device_name, len - 1);
    } else if (s_bonded_name[0]) {
        strncpy(out, s_bonded_name, len - 1);
    } else {
        out[0] = '\0';
    }
    out[len - 1] = '\0';
    BLE_UNLOCK();
}
