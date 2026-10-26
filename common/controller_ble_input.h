#pragma once

#include "rk_ble_hid_host.h"

#ifdef __cplusplus
extern "C" {
#endif

// Shared adapter from BLE media semantics to the controller actor queue.
// Safe to call from the BLE service task or ESP-IDF callbacks.
void controller_ble_input_on_media_key(rk_ble_media_key_t key, void *context);

// Every Consumer Control key down/up from the paired remote. Recognises long
// and double presses and reports them to the bridge as shortcut input; the
// media-key path above is untouched. Safe to call from the BLE service task.
void controller_ble_input_on_consumer_event(uint16_t usage, bool pressed,
                                            void *context);

#ifdef __cplusplus
}
#endif
