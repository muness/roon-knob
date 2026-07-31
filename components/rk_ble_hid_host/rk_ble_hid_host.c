#include "rk_ble_hid_host.h"
#include "rk_ble_hid_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_hid_common.h"
#include "esp_hidh.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nvs.h"
#include "nimble/nimble_port.h"

/* The target owns presentation and controller actions; this file has neither. */

#define TAG "rk_ble_hid_host"
#define NVS_NAMESPACE "ble_remote"
#define NVS_KEY_BDA "bonded_bda"
#define NVS_KEY_ATYPE "bonded_atype"
#define NVS_KEY_NAME "bonded_name"
#define NVS_KEY_ENABLED "enabled"
#define COMMAND_QUEUE_LENGTH 12
#define LIFECYCLE_QUEUE_LENGTH 16
#define EVENT_QUEUE_LENGTH 32
#define MAX_HID_DEVS 4
#define SCAN_DURATION_MS 5000
#define STOP_TIMEOUT_MS 45000
#define RECONNECT_INITIAL_DELAY_MS 2000
#define RECONNECT_MAX_DELAY_MS 30000
#define MAX_RECONNECT_ATTEMPTS 20
#define START_RETRY_INITIAL_DELAY_MS 1000
#define START_RETRY_MAX_DELAY_MS 30000
#define MAX_START_RETRY_ATTEMPTS 5
#define HOST_EXITED_BIT BIT0
#define STOP_CALL_DONE_BIT BIT1
#define BLE_CORE 0

void ble_store_config_init(void);

typedef enum {
    COMMAND_BOOT,
    COMMAND_SET_ENABLED,
    COMMAND_SCAN,
    COMMAND_PAIR,
    COMMAND_FORGET,
} command_type_t;

typedef struct {
    command_type_t type;
    union {
        bool enabled;
        rk_ble_hid_host_device_t device;
    } data;
} command_t;

typedef enum {
    EVENT_SYNC,
    EVENT_OPEN,
    EVENT_CONNECT_CALL_DONE,
    EVENT_CLOSE,
    EVENT_INPUT,
    EVENT_DISCOVERY,
    EVENT_DISCOVERY_DONE,
} event_type_t;

typedef struct {
    event_type_t type;
    union {
        struct {
            esp_err_t status;
            bool tracked;
            uint8_t bda[6];
            char name[RK_BLE_HID_HOST_NAME_MAX_LEN];
        } open;
        struct {
            bool succeeded;
            uint32_t generation;
        } connect_done;
        struct {
            bool tracked;
            uint8_t bda[6];
            int reason;
        } close;
        struct {
            uint8_t data[16];
            uint16_t len;
            esp_hid_usage_t usage;
            uint8_t report_id;
        } input;
        struct {
            uint8_t bda[6];
            uint8_t addr_type;
            uint8_t data[64];
            uint8_t len;
            uint32_t generation;
        } discovery;
        uint32_t generation;
    } data;
} service_event_t;

typedef struct {
    esp_hidh_dev_t *dev;
    bool close_requested;
    uint8_t bda[6];
} tracked_device_t;

typedef struct {
    rk_ble_hid_host_device_t device;
    uint32_t generation;
} connect_job_t;

typedef struct {
    QueueHandle_t commands;
    QueueHandle_t lifecycle_events;
    QueueHandle_t events;
    EventGroupHandle_t lifecycle;
    SemaphoreHandle_t status_lock;
    SemaphoreHandle_t device_lock;
    TaskHandle_t owner_task;
    rk_ble_hid_host_config_t config;
    rk_ble_hid_host_status_t status;

    /* Everything below is owner-owned unless guarded by device_lock. */
    bool stack_started;
    bool hidh_started;
    bool host_started;
    bool stop_requested;
    bool gap_listener_registered;
    bool forget_pending;
    bool fatal_teardown;
    volatile int stop_result;
    rk_ble_hid_host_error_t stop_completion_error;
    uint8_t own_addr_type;
    uint32_t operation_generation;
    uint32_t opening_generation;
    uint8_t opening_addr_type;
    char opening_name[RK_BLE_HID_HOST_NAME_MAX_LEN];
    volatile uint32_t callback_scan_generation;
    TickType_t scan_deadline;
    TickType_t stop_deadline;
    TickType_t reconnect_due;
    uint32_t reconnect_generation;
    uint8_t reconnect_attempts;
    TickType_t start_retry_due;
    uint8_t start_retry_attempts;
    bool start_retry_reload_nvs;
    rk_ble_hid_host_error_t start_retry_error;
    bool connect_worker_active;
    bool connect_callback_pending;
    uint32_t connect_callback_generation;
    uint32_t open_event_seen_generation;
    /* The callback updates this registry before IDF invalidates event pointers. */
    tracked_device_t devices[MAX_HID_DEVS];
    size_t close_events_pending;
    bool have_active_bda;
    uint8_t active_bda[6];
    uint8_t bonded_bda[6];
    uint8_t bonded_addr_type;
    bool have_forget_peer;
    uint8_t forget_bda[6];
    uint8_t forget_addr_type;
    struct ble_gap_event_listener gap_listener;
} service_t;

static service_t s;

static void log_memory(const char *stage) {
    ESP_LOGI(TAG, "%s: internal free=%u largest=%u PSRAM free=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static rk_ble_hid_host_state_t status_state(void);

static void copy_name(char destination[RK_BLE_HID_HOST_NAME_MAX_LEN], const char *source) {
    if (!source) {
        destination[0] = '\0';
        return;
    }
    const size_t length =
        strnlen(source, RK_BLE_HID_HOST_NAME_MAX_LEN - 1);
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void format_bda_name(char destination[RK_BLE_HID_HOST_NAME_MAX_LEN],
                            const uint8_t bda[6]) {
    snprintf(destination, RK_BLE_HID_HOST_NAME_MAX_LEN,
             "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2],
             bda[3], bda[4], bda[5]);
}

static bool is_stopping(void) {
    return status_state() == RK_BLE_HID_HOST_STATE_STOPPING;
}

static void publish_status(void) {
    rk_ble_hid_host_status_t snapshot;
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    snapshot = s.status;
    xSemaphoreGive(s.status_lock);
    if (s.config.on_status) {
        s.config.on_status(&snapshot, s.config.callback_context);
    }
}

static void set_state(rk_ble_hid_host_state_t state, rk_ble_hid_host_error_t error) {
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    s.status.state = state;
    s.status.last_error = error;
    xSemaphoreGive(s.status_lock);
    publish_status();
}

static rk_ble_hid_host_state_t status_state(void) {
    if (!s.status_lock) {
        return RK_BLE_HID_HOST_STATE_UNAVAILABLE;
    }
    rk_ble_hid_host_state_t state;
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    state = s.status.state;
    xSemaphoreGive(s.status_lock);
    return state;
}

static void status_set_enabled(bool enabled) {
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    s.status.enabled = enabled;
    xSemaphoreGive(s.status_lock);
}

static void status_set_connection(bool connected, const char *name) {
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    s.status.connected = connected;
    copy_name(s.status.active_name, connected ? name : NULL);
    xSemaphoreGive(s.status_lock);
}

static void status_set_bond(bool bonded, const char *name) {
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    s.status.bonded = bonded;
    copy_name(s.status.bonded_name, bonded ? name : NULL);
    xSemaphoreGive(s.status_lock);
}

static esp_err_t nvs_set_enabled(bool enabled) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, NVS_KEY_ENABLED, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t nvs_load(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t enabled;
    err = nvs_get_u8(handle, NVS_KEY_ENABLED, &enabled);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        enabled =
            rk_ble_hid_policy_resolve_enabled(false, 0,
                                              s.config.default_enabled)
                ? 1
                : 0;
        err = nvs_set_u8(handle, NVS_KEY_ENABLED, enabled);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
    }
    if (err == ESP_OK) {
        status_set_enabled(rk_ble_hid_policy_resolve_enabled(
            true, enabled, s.config.default_enabled));
    }

    size_t bda_len = sizeof(s.bonded_bda);
    if (nvs_get_blob(handle, NVS_KEY_BDA, s.bonded_bda, &bda_len) == ESP_OK &&
        bda_len == sizeof(s.bonded_bda) &&
        nvs_get_u8(handle, NVS_KEY_ATYPE, &s.bonded_addr_type) == ESP_OK) {
        char name[RK_BLE_HID_HOST_NAME_MAX_LEN] = "";
        size_t name_len = sizeof(name);
        nvs_get_str(handle, NVS_KEY_NAME, name, &name_len);
        status_set_bond(true, name);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t nvs_save_bond(const uint8_t bda[6], uint8_t addr_type, const char *name) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, NVS_KEY_BDA, bda, 6);
    if (err == ESP_OK) err = nvs_set_u8(handle, NVS_KEY_ATYPE, addr_type);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_KEY_NAME, name ? name : "");
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        memcpy(s.bonded_bda, bda, sizeof(s.bonded_bda));
        s.bonded_addr_type = addr_type;
        status_set_bond(true, name);
    }
    return err;
}

static esp_err_t nvs_clear_bond(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, NVS_KEY_BDA);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_erase_key(handle, NVS_KEY_ATYPE);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_erase_key(handle, NVS_KEY_NAME);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        memset(s.bonded_bda, 0, sizeof(s.bonded_bda));
        status_set_bond(false, NULL);
    }
    return err;
}

static bool track_device_from_callback(esp_hidh_dev_t *dev,
                                       uint8_t bda_out[6]) {
    if (!dev || !s.device_lock) return false;
    const uint8_t *bda = esp_hidh_dev_bda_get(dev);
    if (!bda) return false;
    bool tracked = false;
    xSemaphoreTake(s.device_lock, portMAX_DELAY);
    for (size_t i = 0; i < MAX_HID_DEVS; ++i) {
        if (s.devices[i].dev == dev) {
            memcpy(bda_out, s.devices[i].bda, 6);
            tracked = true;
            break;
        }
        if (!s.devices[i].dev) {
            s.devices[i].dev = dev;
            s.devices[i].close_requested = false;
            memcpy(s.devices[i].bda, bda, sizeof(s.devices[i].bda));
            memcpy(bda_out, bda, 6);
            tracked = true;
            break;
        }
    }
    xSemaphoreGive(s.device_lock);
    if (!tracked) ESP_LOGW(TAG, "No tracking slot for HID device");
    return tracked;
}

static bool untrack_device_from_callback(esp_hidh_dev_t *dev,
                                         uint8_t bda_out[6]) {
    if (!dev || !s.device_lock) return false;
    bool tracked = false;
    xSemaphoreTake(s.device_lock, portMAX_DELAY);
    for (size_t i = 0; i < MAX_HID_DEVS; ++i) {
        if (s.devices[i].dev == dev) {
            memcpy(bda_out, s.devices[i].bda, 6);
            s.devices[i] = (tracked_device_t){0};
            tracked = true;
            break;
        }
    }
    if (tracked) ++s.close_events_pending;
    xSemaphoreGive(s.device_lock);
    return tracked;
}

static void acknowledge_close_event(void) {
    xSemaphoreTake(s.device_lock, portMAX_DELAY);
    if (s.close_events_pending) --s.close_events_pending;
    xSemaphoreGive(s.device_lock);
}

static bool have_devices(void) {
    bool have_any = false;
    xSemaphoreTake(s.device_lock, portMAX_DELAY);
    for (size_t i = 0; i < MAX_HID_DEVS; ++i) {
        if (s.devices[i].dev) {
            have_any = true;
            break;
        }
    }
    have_any = have_any || s.close_events_pending != 0;
    xSemaphoreGive(s.device_lock);
    return have_any;
}

static void close_device_once_by_bda(const uint8_t bda[6]) {
    esp_hidh_dev_t *dev = NULL;
    xSemaphoreTake(s.device_lock, portMAX_DELAY);
    for (size_t i = 0; i < MAX_HID_DEVS; ++i) {
        if (s.devices[i].dev &&
            memcmp(s.devices[i].bda, bda, sizeof(s.devices[i].bda)) == 0) {
            if (!s.devices[i].close_requested) {
                s.devices[i].close_requested = true;
                dev = s.devices[i].dev;
            }
            break;
        }
    }
    xSemaphoreGive(s.device_lock);
    if (!dev) return;
    /*
     * IDF 5.5.5 closes through lock_known_device(): it holds IDF's device-list
     * lock, compares identity first, and acquires the device mutex before any
     * dereference. A simultaneous remote CLOSE therefore makes this return
     * ESP_FAIL safely instead of dereferencing the retired callback pointer.
     */
    const esp_err_t err = esp_hidh_dev_close(dev);
    if (err != ESP_OK) {
        bool still_tracked = false;
        xSemaphoreTake(s.device_lock, portMAX_DELAY);
        for (size_t i = 0; i < MAX_HID_DEVS; ++i) {
            if (s.devices[i].dev == dev) {
                s.devices[i].close_requested = false;
                still_tracked = true;
                break;
            }
        }
        xSemaphoreGive(s.device_lock);
        if (still_tracked) {
            ESP_LOGE(TAG, "Could not close HID device: %s",
                     esp_err_to_name(err));
        } else {
            ESP_LOGD(TAG, "HID device closed before local close request");
        }
    }
}

static void enqueue_event(const service_event_t *event) {
    if (!s.events || xQueueSend(s.events, event, 0) != pdPASS) {
        ESP_LOGW(TAG, "Non-critical BLE event dropped");
    }
}

static void enqueue_lifecycle_event(const service_event_t *event) {
    if (!s.lifecycle_events ||
        xQueueSend(s.lifecycle_events, event, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "Could not enqueue BLE lifecycle event");
    }
}

static void on_sync(void) {
    const service_event_t event = {.type = EVENT_SYNC};
    enqueue_lifecycle_event(&event);
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "NimBLE host reset: %d", reason);
}

static void nimble_host_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "NimBLE host task started on core %d; stack free=%u",
             xPortGetCoreID(),
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    log_memory("before nimble_port_run");
    nimble_port_run();
    /* This acknowledgement is the owner task's proof that run() returned. */
    xEventGroupSetBits(s.lifecycle, HOST_EXITED_BIT);
    vTaskDelete(NULL);
}

static void nimble_stop_task(void *arg) {
    (void)arg;
    s.stop_result = nimble_port_stop();
    xEventGroupSetBits(s.lifecycle, STOP_CALL_DONE_BIT);
    vTaskDelete(NULL);
}

static void connect_worker_task(void *arg) {
    connect_job_t *job = arg;
    service_event_t event = {
        .type = EVENT_CONNECT_CALL_DONE,
        .data.connect_done.generation = job->generation,
    };
    uint8_t bda[6];
    memcpy(bda, job->device.bda, sizeof(bda));
    event.data.connect_done.succeeded =
        esp_hidh_dev_open(bda, ESP_HID_TRANSPORT_BLE,
                          job->device.addr_type) != NULL;
    free(job);
    enqueue_lifecycle_event(&event);
    vTaskDelete(NULL);
}

static void hidh_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    (void)handler_args;
    (void)base;
    const esp_hidh_event_t event_id = (esp_hidh_event_t)id;
    const esp_hidh_event_data_t *event_data_hidh = event_data;
    service_event_t event = {0};
    switch (event_id) {
    case ESP_HIDH_OPEN_EVENT:
        event.type = EVENT_OPEN;
        event.data.open.status = event_data_hidh->open.status;
        /*
         * IDF invalidates failed-OPEN and all CLOSE device pointers after this
         * callback returns. Register a successful device now and carry only
         * copied identity/status through the owner queue.
         */
        esp_hidh_dev_t *opened = rk_ble_hid_policy_open_device(
            event_data_hidh->open.status, event_data_hidh->open.dev);
        if (opened) {
            event.data.open.tracked = track_device_from_callback(
                opened, event.data.open.bda);
            copy_name(event.data.open.name, esp_hidh_dev_name_get(opened));
            if (!event.data.open.tracked) {
                event.data.open.status = ESP_ERR_NO_MEM;
                esp_hidh_dev_close(opened);
            }
        }
        enqueue_lifecycle_event(&event);
        break;
    case ESP_HIDH_CLOSE_EVENT:
        event.type = EVENT_CLOSE;
        event.data.close.reason = event_data_hidh->close.reason;
        event.data.close.tracked = untrack_device_from_callback(
            event_data_hidh->close.dev, event.data.close.bda);
        enqueue_lifecycle_event(&event);
        break;
    case ESP_HIDH_INPUT_EVENT:
        event.type = EVENT_INPUT;
        event.data.input.usage = event_data_hidh->input.usage;
        event.data.input.report_id = event_data_hidh->input.report_id;
        event.data.input.len = event_data_hidh->input.length;
        if (event.data.input.len > sizeof(event.data.input.data)) {
            event.data.input.len = sizeof(event.data.input.data);
        }
        if (event.data.input.len) {
            memcpy(event.data.input.data, event_data_hidh->input.data, event.data.input.len);
        }
        const bool is_release = event.data.input.len >= 2 &&
                                event.data.input.data[0] == 0 &&
                                event.data.input.data[1] == 0;
        if (is_release) {
            ESP_LOGD(TAG, "HID media key release report_id=%u",
                     (unsigned)event.data.input.report_id);
        } else {
            ESP_LOGI(TAG, "HID input usage=%s report_id=%u len=%u",
                     esp_hid_usage_str(event.data.input.usage),
                     (unsigned)event.data.input.report_id,
                     (unsigned)event.data.input.len);
            if (event.data.input.len) {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, event.data.input.data,
                                         event.data.input.len, ESP_LOG_INFO);
            }
        }
        enqueue_event(&event);
        break;
    default:
        break;
    }
}

static int scan_event(struct ble_gap_event *gap_event, void *arg) {
    (void)arg;
    service_event_t event = {0};
    if (gap_event->type == BLE_GAP_EVENT_DISC) {
        event.type = EVENT_DISCOVERY;
        memcpy(event.data.discovery.bda, gap_event->disc.addr.val, 6);
        event.data.discovery.addr_type = gap_event->disc.addr.type;
        event.data.discovery.len = gap_event->disc.length_data;
        if (event.data.discovery.len > sizeof(event.data.discovery.data)) {
            event.data.discovery.len = sizeof(event.data.discovery.data);
        }
        memcpy(event.data.discovery.data, gap_event->disc.data, event.data.discovery.len);
        event.data.discovery.generation = s.callback_scan_generation;
        enqueue_event(&event);
    } else if (gap_event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        event.type = EVENT_DISCOVERY_DONE;
        event.data.generation = s.callback_scan_generation;
        enqueue_event(&event);
    }
    return 0;
}

static int gap_listener_event(struct ble_gap_event *gap_event, void *arg) {
    (void)arg;
    if (gap_event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc description;
        if (ble_gap_conn_find(gap_event->enc_change.conn_handle,
                              &description) == 0) {
            ESP_LOGI(TAG,
                     "Encryption change: status=%d encrypted=%d authenticated=%d bonded=%d",
                     gap_event->enc_change.status,
                     description.sec_state.encrypted,
                     description.sec_state.authenticated,
                     description.sec_state.bonded);
        }
    } else if (gap_event->type == BLE_GAP_EVENT_REPEAT_PAIRING) {
        struct ble_gap_conn_desc description;
        if (ble_gap_conn_find(gap_event->repeat_pairing.conn_handle, &description) == 0) {
            ble_store_util_delete_peer(&description.peer_id_addr);
            ESP_LOGI(TAG, "Repeat pairing: removed stale peer bond");
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    return 0;
}

static bool delete_forget_peer(void) {
    if (!s.have_forget_peer) return true;
    if (!s.stack_started) return false;
    ble_addr_t peer = {0};
    peer.type = s.forget_addr_type;
    memcpy(peer.val, s.forget_bda, sizeof(peer.val));
    const int rc = ble_store_util_delete_peer(&peer);
    if (rc != 0 && rc != BLE_HS_ENOENT) {
        ESP_LOGE(TAG, "Could not delete NimBLE peer record: %d", rc);
        return false;
    }
    s.have_forget_peer = false;
    return true;
}

static bool clear_forget_identity(void) {
    if (!delete_forget_peer()) {
        set_state(RK_BLE_HID_HOST_STATE_ERROR,
                  RK_BLE_HID_HOST_ERROR_BOND_STORE);
        return false;
    }
    if (nvs_clear_bond() != ESP_OK) {
        set_state(RK_BLE_HID_HOST_STATE_ERROR, RK_BLE_HID_HOST_ERROR_NVS);
        return false;
    }
    return true;
}

static void request_stop(void);

static void schedule_start_retry(rk_ble_hid_host_error_t error,
                                 bool reload_nvs) {
    set_state(RK_BLE_HID_HOST_STATE_ERROR, error);
    s.start_retry_error = error;
    s.start_retry_reload_nvs = reload_nvs;
    if (s.fatal_teardown || (!reload_nvs && !s.status.enabled) ||
        s.start_retry_attempts >= MAX_START_RETRY_ATTEMPTS) {
        s.start_retry_due = 0;
        ESP_LOGE(TAG, "BLE startup stopped after %u attempts (%s)",
                 (unsigned)s.start_retry_attempts,
                 rk_ble_hid_host_error_name(error));
        return;
    }
    uint32_t delay_ms =
        START_RETRY_INITIAL_DELAY_MS
        << (s.start_retry_attempts < 5 ? s.start_retry_attempts : 5);
    if (delay_ms > START_RETRY_MAX_DELAY_MS) {
        delay_ms = START_RETRY_MAX_DELAY_MS;
    }
    ++s.start_retry_attempts;
    s.start_retry_due =
        xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    ESP_LOGW(TAG, "BLE startup retry %u/%u in %lu ms (%s)",
             (unsigned)s.start_retry_attempts,
             (unsigned)MAX_START_RETRY_ATTEMPTS, (unsigned long)delay_ms,
             rk_ble_hid_host_error_name(error));
}

static void start_stack(void) {
    if (s.fatal_teardown || s.stack_started) return;
    set_state(RK_BLE_HID_HOST_STATE_STARTING, RK_BLE_HID_HOST_ERROR_NONE);
    log_memory("before nimble_port_init");
    if (nimble_port_init() != ESP_OK) {
        schedule_start_retry(RK_BLE_HID_HOST_ERROR_NIMBLE_INIT, false);
        return;
    }
    s.stack_started = true;
    log_memory("after nimble_port_init");
    xEventGroupClearBits(s.lifecycle, HOST_EXITED_BIT);
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();

    esp_hidh_config_t hidh_config = {
        .callback = hidh_event,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    if (esp_hidh_init(&hidh_config) != ESP_OK) {
        nimble_port_deinit();
        s.stack_started = false;
        schedule_start_retry(RK_BLE_HID_HOST_ERROR_HID_INIT, false);
        return;
    }
    s.hidh_started = true;
    /*
     * ESP-IDF 5.5's NimBLE HID wrapper overwrites sm_io_cap with
     * BLE_HS_IO_KEYBOARD_ONLY. Media remotes have no passkey-entry path, so
     * restore the host's Just Works policy after the wrapper is initialized.
     * The wrapper owns security initiation for its connection; the GAP
     * listener below only observes encryption and handles stale bonds.
     */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    log_memory("after esp_hidh_init");
    const int listener_rc =
        ble_gap_event_listener_register(&s.gap_listener, gap_listener_event, NULL);
    if (listener_rc != 0) {
        ESP_LOGE(TAG, "Could not register GAP listener: %d", listener_rc);
        esp_hidh_deinit();
        nimble_port_deinit();
        s.hidh_started = false;
        s.stack_started = false;
        schedule_start_retry(RK_BLE_HID_HOST_ERROR_NIMBLE_INIT, false);
        return;
    }
    s.gap_listener_registered = true;
    if (xTaskCreatePinnedToCore(nimble_host_task, "rk_ble_host", 4096, NULL,
                                5, NULL, BLE_CORE) != pdPASS) {
        ble_gap_event_listener_unregister(&s.gap_listener);
        s.gap_listener_registered = false;
        esp_hidh_deinit();
        nimble_port_deinit();
        s.hidh_started = false;
        s.stack_started = false;
        schedule_start_retry(RK_BLE_HID_HOST_ERROR_NIMBLE_INIT, false);
        return;
    }
    s.host_started = true;
    log_memory("after NimBLE host task creation");
}

static void close_all_devices(void) {
    uint8_t bdas[MAX_HID_DEVS][6] = {0};
    size_t count = 0;
    xSemaphoreTake(s.device_lock, portMAX_DELAY);
    for (size_t i = 0; i < MAX_HID_DEVS; ++i) {
        if (s.devices[i].dev && count < MAX_HID_DEVS) {
            memcpy(bdas[count++], s.devices[i].bda, 6);
        }
    }
    xSemaphoreGive(s.device_lock);
    for (size_t i = 0; i < count; ++i) {
        close_device_once_by_bda(bdas[i]);
    }
}

static void fail_teardown(const char *step, int error) {
    ESP_LOGE(TAG, "BLE teardown failed at %s: %d; reboot required", step,
             error);
    s.fatal_teardown = true;
    set_state(RK_BLE_HID_HOST_STATE_ERROR,
              RK_BLE_HID_HOST_ERROR_TEARDOWN);
}

static void finish_stop_if_ready(void) {
    if (!is_stopping() ||
        !rk_ble_hid_policy_stop_ready(s.connect_worker_active,
                                      s.connect_callback_pending,
                                      have_devices())) {
        return;
    }
    if (s.hidh_started) {
        const esp_err_t err = esp_hidh_deinit();
        if (err != ESP_OK) {
            fail_teardown("esp_hidh_deinit", err);
            return;
        }
        s.hidh_started = false;
    }
    if (s.gap_listener_registered) {
        const int rc = ble_gap_event_listener_unregister(&s.gap_listener);
        if (rc != 0) {
            fail_teardown("ble_gap_event_listener_unregister", rc);
            return;
        }
        s.gap_listener_registered = false;
    }
    if (s.host_started && !s.stop_requested) {
        s.stop_requested = true;
        s.stop_result = ESP_FAIL;
        xEventGroupClearBits(s.lifecycle, STOP_CALL_DONE_BIT);
        if (xTaskCreatePinnedToCore(nimble_stop_task, "rk_ble_stop", 3072,
                                    NULL, 5, NULL, BLE_CORE) != pdPASS) {
            fail_teardown("nimble stop task creation", ESP_ERR_NO_MEM);
        }
        return;
    }
    if (s.host_started &&
        !(xEventGroupGetBits(s.lifecycle) & STOP_CALL_DONE_BIT)) {
        return;
    }
    if (s.host_started && s.stop_result != ESP_OK) {
        fail_teardown("nimble_port_stop", s.stop_result);
        return;
    }
    if (s.host_started && !(xEventGroupGetBits(s.lifecycle) & HOST_EXITED_BIT)) return;
    if (s.stack_started) {
        const esp_err_t err = nimble_port_deinit();
        if (err != ESP_OK) {
            fail_teardown("nimble_port_deinit", err);
            return;
        }
    }
    s.stack_started = false;
    s.host_started = false;
    s.stop_requested = false;
    const rk_ble_hid_host_error_t completion_error =
        s.stop_completion_error;
    s.stop_completion_error = RK_BLE_HID_HOST_ERROR_NONE;
    if (s.status.enabled && s.start_retry_due && !s.forget_pending) {
        set_state(RK_BLE_HID_HOST_STATE_ERROR, s.start_retry_error);
    } else if (s.status.enabled && !s.forget_pending) {
        start_stack();
    } else if (s.forget_pending) {
        /* A disabled forget starts only long enough to remove the NimBLE bond. */
        s.forget_pending = false;
        set_state(RK_BLE_HID_HOST_STATE_DISABLED, completion_error);
    } else {
        set_state(RK_BLE_HID_HOST_STATE_DISABLED, completion_error);
    }
}

static void request_stop(void) {
    if (is_stopping()) return;
    ++s.operation_generation;
    s.callback_scan_generation = 0;
    s.reconnect_due = 0;
    if (!s.stack_started) {
        set_state(RK_BLE_HID_HOST_STATE_DISABLED, s.stop_completion_error);
        s.stop_completion_error = RK_BLE_HID_HOST_ERROR_NONE;
        return;
    }
    const bool was_scanning = s.status.state == RK_BLE_HID_HOST_STATE_SCANNING;
    set_state(RK_BLE_HID_HOST_STATE_STOPPING, RK_BLE_HID_HOST_ERROR_NONE);
    s.stop_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(STOP_TIMEOUT_MS);
    if (was_scanning) {
        ble_gap_disc_cancel();
    }
    close_all_devices();
    finish_stop_if_ready();
}

/* Scan storage is separate from status so callers can never retain a mutable index. */
static rk_ble_hid_host_device_t s_scan_results[RK_BLE_HID_HOST_MAX_RESULTS];

static bool scanned_identity_exists(const rk_ble_hid_host_device_t *device) {
    return rk_ble_hid_policy_snapshot_contains(
        device, s_scan_results, s.status.scan_count,
        s.status.scan_generation);
}

static void schedule_reconnect(void) {
    if (s.status.enabled && s.status.bonded && !s.forget_pending && !is_stopping()) {
        if (s.reconnect_attempts >= MAX_RECONNECT_ATTEMPTS) {
            ESP_LOGW(TAG, "Reconnect stopped after %u attempts",
                     (unsigned)s.reconnect_attempts);
            s.reconnect_due = 0;
            return;
        }
        uint32_t delay_ms =
            RECONNECT_INITIAL_DELAY_MS << (s.reconnect_attempts < 4
                                               ? s.reconnect_attempts
                                               : 4);
        if (delay_ms > RECONNECT_MAX_DELAY_MS) {
            delay_ms = RECONNECT_MAX_DELAY_MS;
        }
        ++s.reconnect_attempts;
        s.reconnect_generation = s.operation_generation;
        s.reconnect_due = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    }
}

static void connect_device(const rk_ble_hid_host_device_t *device) {
    if (s.connect_worker_active || s.connect_callback_pending) {
        set_state(RK_BLE_HID_HOST_STATE_READY,
                  RK_BLE_HID_HOST_ERROR_CONNECT);
        return;
    }
    ++s.operation_generation;
    s.opening_generation = s.operation_generation;
    s.opening_addr_type = device->addr_type;
    copy_name(s.opening_name, device->name);
    set_state(RK_BLE_HID_HOST_STATE_CONNECTING, RK_BLE_HID_HOST_ERROR_NONE);
    connect_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_CONNECT);
        schedule_reconnect();
        return;
    }
    job->device = *device;
    job->generation = s.opening_generation;
    s.connect_worker_active = true;
    if (xTaskCreatePinnedToCore(connect_worker_task, "rk_ble_connect", 5120,
                                job, 4, NULL, BLE_CORE) != pdPASS) {
        s.connect_worker_active = false;
        free(job);
        set_state(RK_BLE_HID_HOST_STATE_READY,
                  RK_BLE_HID_HOST_ERROR_CONNECT);
        schedule_reconnect();
        return;
    }
    ESP_LOGI(TAG, "HID connection attempt %u/%u",
             (unsigned)s.reconnect_attempts,
             (unsigned)MAX_RECONNECT_ATTEMPTS);
}

static void process_command(const command_t *command) {
    switch (command->type) {
    case COMMAND_BOOT:
        if (nvs_load() != ESP_OK) {
            schedule_start_retry(RK_BLE_HID_HOST_ERROR_NVS, true);
        } else if (s.status.enabled) {
            start_stack();
        } else {
            set_state(RK_BLE_HID_HOST_STATE_DISABLED, RK_BLE_HID_HOST_ERROR_NONE);
        }
        break;
    case COMMAND_SET_ENABLED:
        if (s.fatal_teardown) break;
        if (nvs_set_enabled(command->data.enabled) != ESP_OK) {
            set_state(RK_BLE_HID_HOST_STATE_ERROR, RK_BLE_HID_HOST_ERROR_NVS);
            break;
        }
        status_set_enabled(command->data.enabled);
        s.reconnect_attempts = 0;
        s.reconnect_due = 0;
        s.start_retry_attempts = 0;
        s.start_retry_due = 0;
        s.start_retry_reload_nvs = false;
        if (command->data.enabled) {
            if (s.status.state == RK_BLE_HID_HOST_STATE_DISABLED) {
                start_stack();
            } else if (s.status.state == RK_BLE_HID_HOST_STATE_READY) {
                schedule_reconnect();
                publish_status();
            }
        } else {
            request_stop();
        }
        break;
    case COMMAND_SCAN: {
        if (s.status.state != RK_BLE_HID_HOST_STATE_READY) {
            set_state(s.status.state, RK_BLE_HID_HOST_ERROR_INVALID_STATE);
            break;
        }
        ++s.operation_generation;
        xSemaphoreTake(s.status_lock, portMAX_DELAY);
        ++s.status.scan_generation;
        s.callback_scan_generation = s.status.scan_generation;
        s.status.scan_count = 0;
        xSemaphoreGive(s.status_lock);
        memset(s_scan_results, 0, sizeof(s_scan_results));
        struct ble_gap_disc_params parameters = {
            .filter_duplicates = 1,
            .passive = 0,
            .itvl = 0x0050,
            .window = 0x0030,
            .filter_policy = 0,
            .limited = 0,
        };
        if (ble_gap_disc(s.own_addr_type, SCAN_DURATION_MS, &parameters, scan_event, NULL) != 0) {
            set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_SCAN);
        } else {
            s.scan_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SCAN_DURATION_MS + 1000);
            set_state(RK_BLE_HID_HOST_STATE_SCANNING, RK_BLE_HID_HOST_ERROR_NONE);
        }
        break;
    }
    case COMMAND_PAIR:
        if (s.status.state != RK_BLE_HID_HOST_STATE_READY) {
            set_state(s.status.state, RK_BLE_HID_HOST_ERROR_INVALID_STATE);
        } else if (!scanned_identity_exists(&command->data.device)) {
            set_state(s.status.state, RK_BLE_HID_HOST_ERROR_STALE_SCAN);
        } else {
            s.reconnect_attempts = 0;
            connect_device(&command->data.device);
        }
        break;
    case COMMAND_FORGET:
        ++s.operation_generation;
        s.reconnect_due = 0;
        s.reconnect_attempts = 0;
        s.callback_scan_generation = 0;
        if (!s.status.bonded) {
            if (nvs_clear_bond() != ESP_OK) {
                set_state(RK_BLE_HID_HOST_STATE_ERROR,
                          RK_BLE_HID_HOST_ERROR_NVS);
            } else {
                set_state(s.status.enabled ? RK_BLE_HID_HOST_STATE_READY
                                           : RK_BLE_HID_HOST_STATE_DISABLED,
                          RK_BLE_HID_HOST_ERROR_NONE);
            }
            break;
        }
        memcpy(s.forget_bda, s.bonded_bda, sizeof(s.forget_bda));
        s.forget_addr_type = s.bonded_addr_type;
        s.have_forget_peer = true;
        if (!s.stack_started) {
            s.forget_pending = true;
            start_stack();
        } else {
            if (!clear_forget_identity()) {
                break;
            }
            s.forget_pending = true;
            if (s.status.state == RK_BLE_HID_HOST_STATE_SCANNING) ble_gap_disc_cancel();
            close_all_devices();
            if (!s.connect_worker_active && !s.connect_callback_pending &&
                !have_devices()) {
                s.forget_pending = false;
                set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_NONE);
            }
        }
        break;
    }
}

static void process_discovery(const service_event_t *event) {
    if (s.status.state != RK_BLE_HID_HOST_STATE_SCANNING ||
        event->data.discovery.generation != s.status.scan_generation ||
        s.status.scan_count >= RK_BLE_HID_HOST_MAX_RESULTS) return;
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->data.discovery.data, event->data.discovery.len) != 0) return;
    bool is_hid = false;
    for (int i = 0; i < fields.num_uuids16; ++i) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == 0x1812) {
            is_hid = true;
            break;
        }
    }
    if (!is_hid) return;
    for (size_t i = 0; i < s.status.scan_count; ++i) {
        if (memcmp(s_scan_results[i].bda, event->data.discovery.bda, 6) == 0) return;
    }
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    rk_ble_hid_host_device_t *result = &s_scan_results[s.status.scan_count];
    memcpy(result->bda, event->data.discovery.bda, 6);
    result->addr_type = event->data.discovery.addr_type;
    result->scan_generation = s.status.scan_generation;
    if (fields.name && fields.name_len) {
        const size_t name_len = fields.name_len < sizeof(result->name) - 1 ? fields.name_len : sizeof(result->name) - 1;
        memcpy(result->name, fields.name, name_len);
        result->name[name_len] = '\0';
    } else {
        snprintf(result->name, sizeof(result->name), "%02X:%02X:%02X:%02X:%02X:%02X",
                 result->bda[0], result->bda[1], result->bda[2], result->bda[3], result->bda[4], result->bda[5]);
    }
    ++s.status.scan_count;
    xSemaphoreGive(s.status_lock);
    publish_status();
}

static void finalize_open_connection(uint32_t generation) {
    if (generation != s.opening_generation ||
        generation != s.operation_generation || !s.have_active_bda ||
        is_stopping() || s.status.connected) {
        return;
    }
    s.reconnect_attempts = 0;
    status_set_connection(true, s.opening_name);
    if (nvs_save_bond(s.active_bda, s.opening_addr_type,
                      s.opening_name) != ESP_OK) {
        set_state(RK_BLE_HID_HOST_STATE_CONNECTED,
                  RK_BLE_HID_HOST_ERROR_NVS);
    } else {
        set_state(RK_BLE_HID_HOST_STATE_CONNECTED,
                  RK_BLE_HID_HOST_ERROR_NONE);
    }
}

static void process_event(const service_event_t *event) {
    switch (event->type) {
    case EVENT_SYNC:
        if (s.status.state != RK_BLE_HID_HOST_STATE_STARTING) break;
        if (ble_hs_id_infer_auto(0, &s.own_addr_type) != 0) {
            s.stop_completion_error = RK_BLE_HID_HOST_ERROR_SYNC;
            schedule_start_retry(RK_BLE_HID_HOST_ERROR_SYNC, false);
            request_stop();
        } else if (s.forget_pending) {
            if (!clear_forget_identity()) {
                xSemaphoreTake(s.status_lock, portMAX_DELAY);
                s.stop_completion_error = s.status.last_error;
                xSemaphoreGive(s.status_lock);
            }
            request_stop();
        } else {
            s.start_retry_attempts = 0;
            s.start_retry_due = 0;
            s.start_retry_reload_nvs = false;
            set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_NONE);
            schedule_reconnect();
        }
        break;
    case EVENT_OPEN:
        s.open_event_seen_generation = s.opening_generation;
        if (s.connect_callback_pending &&
            s.connect_callback_generation == s.opening_generation) {
            s.connect_callback_pending = false;
        }
        if (event->data.open.status != ESP_OK || !event->data.open.tracked ||
            is_stopping() ||
            s.opening_generation != s.operation_generation) {
            if (event->data.open.tracked) {
                close_device_once_by_bda(event->data.open.bda);
            }
            if (!is_stopping()) {
                set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_CONNECT);
                schedule_reconnect();
            }
            break;
        }
        memcpy(s.active_bda, event->data.open.bda, sizeof(s.active_bda));
        s.have_active_bda = true;
        char resolved_name[RK_BLE_HID_HOST_NAME_MAX_LEN];
        if (event->data.open.name[0]) {
            copy_name(resolved_name, event->data.open.name);
        } else if (s.opening_name[0]) {
            copy_name(resolved_name, s.opening_name);
        } else {
            format_bda_name(resolved_name, event->data.open.bda);
        }
        copy_name(s.opening_name, resolved_name);
        /* OPEN is posted before IDF subscribes to report notifications. Only
         * publish Connected after esp_hidh_dev_open() returns as well. */
        if (!s.connect_worker_active) {
            finalize_open_connection(s.opening_generation);
        }
        break;
    case EVENT_CONNECT_CALL_DONE: {
        const bool current =
            event->data.connect_done.generation == s.opening_generation &&
            event->data.connect_done.generation == s.operation_generation;
        s.connect_worker_active = false;
        if (event->data.connect_done.succeeded) {
            if (s.open_event_seen_generation !=
                event->data.connect_done.generation) {
                s.connect_callback_pending = true;
                s.connect_callback_generation =
                    event->data.connect_done.generation;
            } else if (current) {
                finalize_open_connection(event->data.connect_done.generation);
            }
        } else {
            s.connect_callback_pending = false;
        }
        if (!event->data.connect_done.succeeded && current &&
            !is_stopping()) {
            set_state(RK_BLE_HID_HOST_STATE_READY,
                      RK_BLE_HID_HOST_ERROR_CONNECT);
            schedule_reconnect();
        }
        if (is_stopping()) {
            finish_stop_if_ready();
        } else if (s.forget_pending && !s.connect_callback_pending &&
                   !have_devices()) {
            s.forget_pending = false;
            set_state(RK_BLE_HID_HOST_STATE_READY,
                      RK_BLE_HID_HOST_ERROR_NONE);
        }
        break;
    }
    case EVENT_CLOSE:
        if (event->data.close.tracked) {
            acknowledge_close_event();
        }
        if (s.have_active_bda && event->data.close.tracked &&
            memcmp(s.active_bda, event->data.close.bda,
                   sizeof(s.active_bda)) == 0) {
            s.have_active_bda = false;
            memset(s.active_bda, 0, sizeof(s.active_bda));
            status_set_connection(false, NULL);
        }
        if (is_stopping()) {
            finish_stop_if_ready();
        } else if (s.forget_pending && !s.connect_callback_pending &&
                   !have_devices()) {
            s.forget_pending = false;
            set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_NONE);
        } else if (!s.status.connected) {
            set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_NONE);
            schedule_reconnect();
        }
        break;
    case EVENT_INPUT: {
        rk_ble_media_key_t key;
        const bool mapped = rk_ble_hid_host_map_consumer_report(
            event->data.input.data, event->data.input.len, &key);

        /* ESP-IDF 5.5 can classify a valid Consumer Control report as GENERIC
         * when its HID report-map metadata is incomplete. The proven Frame
         * implementation decoded the payload directly. Preserve that behavior,
         * but only for the exact media usages accepted by the bounded mapper. */
        if (mapped) {
            if (event->data.input.usage != ESP_HID_USAGE_CCONTROL) {
                ESP_LOGW(TAG,
                         "Mapped media report despite ESP-IDF usage=%s "
                         "report_id=%u",
                         esp_hid_usage_str(event->data.input.usage),
                         (unsigned)event->data.input.report_id);
            }
            if (s.config.on_media_key && s.status.connected && !is_stopping()) {
                ESP_LOGI(TAG, "Consumer Control report mapped to media key %d",
                         key);
                s.config.on_media_key(key, s.config.callback_context);
            } else {
                ESP_LOGW(TAG,
                         "Mapped media key %d dropped: connected=%d stopping=%d callback=%d",
                         key, s.status.connected, is_stopping(),
                         s.config.on_media_key != NULL);
            }
        } else if (event->data.input.len >= 2 &&
                   event->data.input.data[0] == 0 &&
                   event->data.input.data[1] == 0) {
            ESP_LOGD(TAG, "HID media key released");
        } else if (event->data.input.usage != ESP_HID_USAGE_CCONTROL) {
            ESP_LOGI(TAG,
                     "HID input ignored: unrecognized payload with usage=%s",
                     esp_hid_usage_str(event->data.input.usage));
        } else {
            ESP_LOGW(TAG,
                     "Unmapped Consumer Control report_id=%u len=%u",
                     (unsigned)event->data.input.report_id,
                     (unsigned)event->data.input.len);
        }
        break;
    }
    case EVENT_DISCOVERY:
        process_discovery(event);
        break;
    case EVENT_DISCOVERY_DONE:
        if (s.status.state == RK_BLE_HID_HOST_STATE_SCANNING &&
            event->data.generation == s.status.scan_generation) {
            s.callback_scan_generation = 0;
            set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_NONE);
        }
        break;
    }
}

static void owner_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "BLE service task started on core %d; stack free=%u",
             xPortGetCoreID(),
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    log_memory("BLE service task start");
    TickType_t next_telemetry = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
    for (;;) {
        command_t command;
        service_event_t event;
        while (xQueueReceive(s.lifecycle_events, &event, 0) == pdPASS) {
            process_event(&event);
        }
        while (xQueueReceive(s.events, &event, 0) == pdPASS) {
            process_event(&event);
        }
        if (xQueueReceive(s.commands, &command, 0) == pdPASS) {
            process_command(&command);
        }

        const TickType_t now = xTaskGetTickCount();
        if (is_stopping()) {
            /* The host-exit bit is set from the NimBLE task, not the queue. */
            finish_stop_if_ready();
        }
        if (is_stopping() && (int32_t)(now - s.stop_deadline) >= 0) {
            s.fatal_teardown = true;
            ESP_LOGE(TAG, "BLE teardown timed out; reboot required");
            set_state(RK_BLE_HID_HOST_STATE_ERROR,
                      RK_BLE_HID_HOST_ERROR_TEARDOWN_TIMEOUT);
        } else if (s.status.state == RK_BLE_HID_HOST_STATE_SCANNING &&
                   (int32_t)(now - s.scan_deadline) >= 0) {
            ble_gap_disc_cancel();
            s.callback_scan_generation = 0;
            set_state(RK_BLE_HID_HOST_STATE_READY, RK_BLE_HID_HOST_ERROR_SCAN);
        } else if (s.reconnect_due && (int32_t)(now - s.reconnect_due) >= 0 &&
                   s.reconnect_generation == s.operation_generation &&
                   s.status.state == RK_BLE_HID_HOST_STATE_READY) {
            s.reconnect_due = 0;
            rk_ble_hid_host_device_t bonded = {0};
            memcpy(bonded.bda, s.bonded_bda, sizeof(bonded.bda));
            bonded.addr_type = s.bonded_addr_type;
            copy_name(bonded.name, s.status.bonded_name);
            connect_device(&bonded);
        } else if (s.start_retry_due &&
                   (int32_t)(now - s.start_retry_due) >= 0 &&
                   s.status.state == RK_BLE_HID_HOST_STATE_ERROR) {
            const bool reload_nvs = s.start_retry_reload_nvs;
            s.start_retry_due = 0;
            if (reload_nvs) {
                if (nvs_load() != ESP_OK) {
                    schedule_start_retry(RK_BLE_HID_HOST_ERROR_NVS, true);
                } else if (s.status.enabled) {
                    start_stack();
                } else {
                    s.start_retry_attempts = 0;
                    s.start_retry_reload_nvs = false;
                    set_state(RK_BLE_HID_HOST_STATE_DISABLED,
                              RK_BLE_HID_HOST_ERROR_NONE);
                }
            } else {
                start_stack();
            }
        }
        if ((int32_t)(now - next_telemetry) >= 0) {
            ESP_LOGI(TAG, "BLE service stack free=%u",
                     (unsigned)(uxTaskGetStackHighWaterMark(NULL) *
                                sizeof(StackType_t)));
            log_memory("BLE service watermark");
            next_telemetry = now + pdMS_TO_TICKS(60000);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static rk_ble_hid_host_result_t enqueue_command(const command_t *command) {
    if (!s.owner_task) return RK_BLE_HID_HOST_ERR_INVALID_STATE;
    return xQueueSend(s.commands, command, 0) == pdPASS ? RK_BLE_HID_HOST_OK : RK_BLE_HID_HOST_ERR_QUEUE_FULL;
}

rk_ble_hid_host_result_t rk_ble_hid_host_init(const rk_ble_hid_host_config_t *config) {
    if (!config) return RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT;
    if (s.owner_task) return RK_BLE_HID_HOST_ERR_ALREADY_INITIALIZED;
    log_memory("before BLE queues and service task");
    memset(&s, 0, sizeof(s));
    s.config = *config;
    s.status.state = RK_BLE_HID_HOST_STATE_UNAVAILABLE;
    s.commands = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(command_t));
    s.lifecycle_events =
        xQueueCreate(LIFECYCLE_QUEUE_LENGTH, sizeof(service_event_t));
    s.events = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(service_event_t));
    s.lifecycle = xEventGroupCreate();
    s.status_lock = xSemaphoreCreateMutex();
    s.device_lock = xSemaphoreCreateMutex();
    if (!s.commands || !s.lifecycle_events || !s.events || !s.lifecycle ||
        !s.status_lock || !s.device_lock ||
        xTaskCreatePinnedToCore(owner_task, "rk_ble_service", 6144, NULL, 4,
                                &s.owner_task, BLE_CORE) != pdPASS) {
        if (s.owner_task) {
            vTaskDelete(s.owner_task);
        }
        if (s.status_lock) {
            vSemaphoreDelete(s.status_lock);
        }
        if (s.device_lock) {
            vSemaphoreDelete(s.device_lock);
        }
        if (s.lifecycle) {
            vEventGroupDelete(s.lifecycle);
        }
        if (s.events) {
            vQueueDelete(s.events);
        }
        if (s.lifecycle_events) {
            vQueueDelete(s.lifecycle_events);
        }
        if (s.commands) {
            vQueueDelete(s.commands);
        }
        memset(&s, 0, sizeof(s));
        return RK_BLE_HID_HOST_ERR_QUEUE_FULL;
    }
    const command_t boot = {.type = COMMAND_BOOT};
    const rk_ble_hid_host_result_t result = enqueue_command(&boot);
    log_memory(result == RK_BLE_HID_HOST_OK ? "after BLE queues and service task" :
                                               "BLE boot command rejected");
    return result;
}

rk_ble_hid_host_result_t rk_ble_hid_host_set_enabled(bool enabled) {
    const rk_ble_hid_host_state_t state = status_state();
    if (state == RK_BLE_HID_HOST_STATE_STOPPING ||
        (enabled && state == RK_BLE_HID_HOST_STATE_ERROR)) {
        return RK_BLE_HID_HOST_ERR_INVALID_STATE;
    }
    const command_t command = {.type = COMMAND_SET_ENABLED, .data.enabled = enabled};
    return enqueue_command(&command);
}

rk_ble_hid_host_result_t rk_ble_hid_host_scan_start(void) {
    if (status_state() != RK_BLE_HID_HOST_STATE_READY) return RK_BLE_HID_HOST_ERR_INVALID_STATE;
    const command_t command = {.type = COMMAND_SCAN};
    return enqueue_command(&command);
}

size_t rk_ble_hid_host_scan_results_copy(rk_ble_hid_host_device_t *out, size_t capacity,
                                         uint32_t *scan_generation) {
    if (!out || !capacity || !s.status_lock) return 0;
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    const size_t count = s.status.scan_count < capacity ? s.status.scan_count : capacity;
    memcpy(out, s_scan_results, count * sizeof(*out));
    if (scan_generation) *scan_generation = s.status.scan_generation;
    xSemaphoreGive(s.status_lock);
    return count;
}

rk_ble_hid_host_result_t rk_ble_hid_host_pair(const rk_ble_hid_host_device_t *device) {
    if (!device) return RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT;
    if (status_state() != RK_BLE_HID_HOST_STATE_READY) return RK_BLE_HID_HOST_ERR_INVALID_STATE;
    const command_t command = {.type = COMMAND_PAIR, .data.device = *device};
    return enqueue_command(&command);
}

rk_ble_hid_host_result_t rk_ble_hid_host_forget(void) {
    if (!s.owner_task) return RK_BLE_HID_HOST_ERR_INVALID_STATE;
    const rk_ble_hid_host_state_t state = status_state();
    if (state == RK_BLE_HID_HOST_STATE_UNAVAILABLE ||
        state == RK_BLE_HID_HOST_STATE_STARTING ||
        state == RK_BLE_HID_HOST_STATE_STOPPING ||
        state == RK_BLE_HID_HOST_STATE_ERROR) {
        return RK_BLE_HID_HOST_ERR_INVALID_STATE;
    }
    const command_t command = {.type = COMMAND_FORGET};
    return enqueue_command(&command);
}

rk_ble_hid_host_result_t rk_ble_hid_host_status_copy(rk_ble_hid_host_status_t *out) {
    if (!out) return RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT;
    if (!s.status_lock) {
        *out = (rk_ble_hid_host_status_t){
            .state = RK_BLE_HID_HOST_STATE_UNAVAILABLE,
            .last_error = RK_BLE_HID_HOST_ERROR_UNAVAILABLE,
        };
        return RK_BLE_HID_HOST_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s.status_lock, portMAX_DELAY);
    *out = s.status;
    xSemaphoreGive(s.status_lock);
    return RK_BLE_HID_HOST_OK;
}
