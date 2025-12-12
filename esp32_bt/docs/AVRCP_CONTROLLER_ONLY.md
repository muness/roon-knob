# AVRCP Controller-Only Mode (A2DP SDP Hack)

## Problem

We want the Roon Knob to act as a **Bluetooth media controller only** - sending play/pause/next/prev commands to a phone and receiving track metadata via AVRCP. We do **not** want the phone to route audio to the knob (no speakers, no audio processing needed).

However, ESP-IDF's Bluetooth stack has a fundamental limitation: **AVRCP Controller cannot work without A2DP**. The profiles are tightly coupled internally - if you try to use AVRCP without A2DP, phones won't pair or the AVRCP connection fails.

## The Hack

We initialize A2DP Sink (required for AVRCP to function), then immediately **delete its SDP record** so phones can't discover it during service discovery. This gives us:

- Working AVRCP Controller (media controls + metadata)
- Phones pair successfully
- Phones do NOT see us as an audio sink
- No audio routing to the knob

## How It Works

### 1. Normal Bluetooth Service Discovery

When a phone scans for Bluetooth devices, it performs SDP (Service Discovery Protocol) queries to see what services the device supports. Normally, an A2DP Sink advertises itself via an SDP record, causing phones to route audio there.

### 2. The SDP Record Deletion

After initializing A2DP Sink, we access ESP-IDF's internal Bluedroid structures to delete the SDP record:

```c
// Access internal BTA AV control block
#include "bta/bta_av_api.h"
#include "bta_av_int.h"
#include "stack/sdp_api.h"
extern tBTA_AV_CB bta_av_cb;

// In bt_avrcp_init(), after esp_a2d_sink_init():
vTaskDelay(pdMS_TO_TICKS(100));  // Give A2DP time to register SDP
if (bta_av_cb.sdp_a2d_snk_handle != 0) {
    ESP_LOGI(TAG, "Removing A2DP sink SDP record (handle=0x%lx)",
             (unsigned long)bta_av_cb.sdp_a2d_snk_handle);
    SDP_DeleteRecord(bta_av_cb.sdp_a2d_snk_handle);
    bta_av_cb.sdp_a2d_snk_handle = 0;
}
```

### 3. Required CMakeLists.txt Changes

The internal Bluedroid headers aren't exposed by default. Add these include paths:

```cmake
# HACK: Add internal Bluedroid include paths for SDP manipulation
idf_build_get_property(idf_path IDF_PATH)
target_include_directories(__idf_main PRIVATE
    "${idf_path}/components/bt/common/include"
    "${idf_path}/components/bt/host/bluedroid/bta/include"
    "${idf_path}/components/bt/host/bluedroid/bta/av/include"
    "${idf_path}/components/bt/host/bluedroid/stack/include"
    "${idf_path}/components/bt/host/bluedroid/common/include"
)
```

## Risks and Limitations

### 1. Internal API Dependency
This uses **internal, undocumented ESP-IDF APIs**. They may change between ESP-IDF versions without notice. If upgrading ESP-IDF breaks the build:
- Check if `bta_av_cb` structure still exists
- Check if `sdp_a2d_snk_handle` field is still present
- Check if `SDP_DeleteRecord()` signature changed

### 2. Timing Sensitivity
The 100ms delay is empirical. If A2DP hasn't registered its SDP record yet, the hack won't work. Increase the delay if you see "A2DP sink SDP handle not found" warnings.

### 3. A2DP Still Exists Internally
A2DP is initialized and running internally - we just hide it from service discovery. If a phone somehow connects to A2DP directly (unlikely), audio data will be ignored (NULL data callback).

## Verification

On successful startup, you should see:
```
I (xxx) bt_avrcp: Removing A2DP sink SDP record (handle=0x10001)
```

If you see this warning instead, the hack didn't work:
```
W (xxx) bt_avrcp: A2DP sink SDP handle not found - cannot hide A2DP
```

## Alternative Approaches Considered

1. **AVRCP without A2DP**: Doesn't work - ESP-IDF requires A2DP for AVRCP Controller
2. **A2DP Source instead of Sink**: Wrong profile - Source is for sending audio TO speakers
3. **BLE AVRCP**: Doesn't exist - AVRCP is Classic Bluetooth only
4. **Disable audio codec**: A2DP would still be discovered, phone would try to connect

## Tested Configurations

- ESP-IDF: v5.4.3
- Target: ESP32 (not S3)
- Phone: iPhone (iOS 17+)
- Result: Pairs successfully, no audio routing, metadata + controls work

## References

- ESP-IDF Bluedroid source: `components/bt/host/bluedroid/`
- BTA AV internals: `bta/av/include/bta_av_int.h`
- SDP API: `stack/include/stack/sdp_api.h`
