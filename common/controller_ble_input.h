#pragma once

#include "rk_ble_hid_host.h"

#ifdef __cplusplus
extern "C" {
#endif

// Shared adapter from BLE media semantics to the controller actor queue.
// Safe to call from the BLE service task or ESP-IDF callbacks.
void controller_ble_input_on_media_key(rk_ble_media_key_t key, void *context);

#ifdef __cplusplus
}
#endif
