#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RK_BLE_HID_HOST_MAX_RESULTS 8
#define RK_BLE_HID_HOST_NAME_MAX_LEN 64

/** Stable lifecycle states. STOPPING is observable until the NimBLE host exits. */
typedef enum {
    RK_BLE_HID_HOST_STATE_UNAVAILABLE = 0,
    RK_BLE_HID_HOST_STATE_DISABLED,
    RK_BLE_HID_HOST_STATE_STARTING,
    RK_BLE_HID_HOST_STATE_READY,
    RK_BLE_HID_HOST_STATE_SCANNING,
    RK_BLE_HID_HOST_STATE_CONNECTING,
    RK_BLE_HID_HOST_STATE_CONNECTED,
    RK_BLE_HID_HOST_STATE_STOPPING,
    RK_BLE_HID_HOST_STATE_ERROR,
} rk_ble_hid_host_state_t;

typedef enum {
    RK_BLE_MEDIA_KEY_NONE = 0,
    RK_BLE_MEDIA_KEY_PLAY_PAUSE,
    RK_BLE_MEDIA_KEY_NEXT_TRACK,
    RK_BLE_MEDIA_KEY_PREVIOUS_TRACK,
    RK_BLE_MEDIA_KEY_VOLUME_UP,
    RK_BLE_MEDIA_KEY_VOLUME_DOWN,
} rk_ble_media_key_t;

typedef enum {
    RK_BLE_HID_HOST_ERROR_NONE = 0,
    RK_BLE_HID_HOST_ERROR_UNAVAILABLE,
    RK_BLE_HID_HOST_ERROR_NVS,
    RK_BLE_HID_HOST_ERROR_NIMBLE_INIT,
    RK_BLE_HID_HOST_ERROR_HID_INIT,
    RK_BLE_HID_HOST_ERROR_SYNC,
    RK_BLE_HID_HOST_ERROR_SCAN,
    RK_BLE_HID_HOST_ERROR_CONNECT,
    RK_BLE_HID_HOST_ERROR_BOND_STORE,
    RK_BLE_HID_HOST_ERROR_INVALID_STATE,
    RK_BLE_HID_HOST_ERROR_STALE_SCAN,
    RK_BLE_HID_HOST_ERROR_TEARDOWN,
    RK_BLE_HID_HOST_ERROR_TEARDOWN_TIMEOUT,
} rk_ble_hid_host_error_t;

/** Immediate command result. Completion and detailed failures arrive in status. */
typedef enum {
    RK_BLE_HID_HOST_OK = 0,
    RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT,
    RK_BLE_HID_HOST_ERR_NOT_SUPPORTED,
    RK_BLE_HID_HOST_ERR_ALREADY_INITIALIZED,
    RK_BLE_HID_HOST_ERR_QUEUE_FULL,
    RK_BLE_HID_HOST_ERR_INVALID_STATE,
} rk_ble_hid_host_result_t;

const char *rk_ble_hid_host_result_name(rk_ble_hid_host_result_t result);
const char *rk_ble_hid_host_error_name(rk_ble_hid_host_error_t error);

/** A scan result is an identity value, never a mutable result-array index. */
typedef struct {
    char name[RK_BLE_HID_HOST_NAME_MAX_LEN];
    uint8_t bda[6];
    uint8_t addr_type;
    uint32_t scan_generation;
} rk_ble_hid_host_device_t;

typedef struct {
    bool enabled;
    bool connected;
    bool bonded;
    rk_ble_hid_host_state_t state;
    rk_ble_hid_host_error_t last_error;
    char bonded_name[RK_BLE_HID_HOST_NAME_MAX_LEN];
    char active_name[RK_BLE_HID_HOST_NAME_MAX_LEN];
    uint32_t scan_generation;
    size_t scan_count;
} rk_ble_hid_host_status_t;

typedef void (*rk_ble_hid_host_media_callback_t)(rk_ble_media_key_t key, void *context);
typedef void (*rk_ble_hid_host_status_callback_t)(const rk_ble_hid_host_status_t *status,
                                                   void *context);

typedef struct {
    /** Used only when the local `ble_remote/enabled` key has not been written. */
    bool default_enabled;
    rk_ble_hid_host_media_callback_t on_media_key;
    rk_ble_hid_host_status_callback_t on_status;
    void *callback_context;
} rk_ble_hid_host_config_t;

/**
 * Create the single service owner and enqueue its initial lifecycle command.
 * Returns when accepted; readiness and failures are reported through status.
 */
rk_ble_hid_host_result_t rk_ble_hid_host_init(const rk_ble_hid_host_config_t *config);

/** Persist and enqueue a lifecycle transition. Calls are accepted asynchronously. */
rk_ble_hid_host_result_t rk_ble_hid_host_set_enabled(bool enabled);

/** Start a bounded five-second HID scan. */
rk_ble_hid_host_result_t rk_ble_hid_host_scan_start(void);

/** Copy one coherent, generation-tagged scan snapshot. */
size_t rk_ble_hid_host_scan_results_copy(rk_ble_hid_host_device_t *out, size_t capacity,
                                         uint32_t *scan_generation);

/** Pair the copied identity returned by rk_ble_hid_host_scan_results_copy(). */
rk_ble_hid_host_result_t rk_ble_hid_host_pair(const rk_ble_hid_host_device_t *device);

/** Erase local metadata and the NimBLE security record for the bonded peer. */
rk_ble_hid_host_result_t rk_ble_hid_host_forget(void);

/** Copy the current status without exposing service-owned storage. */
rk_ble_hid_host_result_t rk_ble_hid_host_status_copy(rk_ble_hid_host_status_t *out);

/**
 * Pure payload mapping independent of ESP-IDF's report usage classification.
 * Returns false for releases, unknown usages, and short reports.
 */
bool rk_ble_hid_host_map_consumer_report(const uint8_t *data, size_t len,
                                         rk_ble_media_key_t *key);

#ifdef __cplusplus
}
#endif
