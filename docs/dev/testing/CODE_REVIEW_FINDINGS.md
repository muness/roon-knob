# Code Review Findings

Code path analysis for test scripts, identifying potential regressions.

**Review Date:** December 2025
**Scope:** WiFi/Roon mode and Bluetooth mode code paths

## Summary

| Category | Critical | High | Medium | Low |
|----------|----------|------|--------|-----|
| Fixed | 1 | 0 | 0 | 0 |
| Known Issues | 0 | 1 | 3 | 2 |
| False Positives | 2 | 3 | 2 | 0 |

## Fixed Issues

### 1. Double BT Activation on Boot (CRITICAL - FIXED)

**Problem:** When booting in Bluetooth mode, BT was activated twice:
1. Via `mode_change_callback()` triggered by `app_entry()`
2. Again in `app_main()` after checking mode

**Fix:** Removed duplicate activation in `main_idf.c:427-436`. Now relies solely on the callback-triggered activation.

**File:** `idf_app/main/main_idf.c:427-436`

---

## Known Issues (Not Blocking)

### HIGH: esp32_comm_deinit() Never Called

**Problem:** When exiting Bluetooth mode, UART resources aren't cleaned up:
- RX task continues running
- Heartbeat timer keeps firing

**Impact:** Minor resource leak. Doesn't cause crashes because:
- RX task just reads empty buffer
- Heartbeat pings are ignored by idle ESP32

**Mitigation:** Could add cleanup call in mode_change_callback when exiting BT mode.

**File:** `idf_app/main/esp32_comm.c:463-477`

---

### MEDIUM: Artwork Refresh Flag Race

**Problem:** `s_force_artwork_refresh` is written inside lock but read outside lock in UI callback.

**Impact:** Unlikely to cause issues in practice because:
- Single writer (zone change code)
- Single reader (UI callback)
- Boolean flag - worst case is missed refresh

**File:** `common/bridge_client.c:102,679`

---

### MEDIUM: Captive Portal Reboot Timing

**Problem:** 2-second delay before reboot may not be sufficient for HTTP response to fully transmit.

**Impact:** User might see incomplete success message. Config IS saved correctly before response.

**Mitigation:** Could use `httpd_resp_send()` return value and wait for ACK.

**File:** `idf_app/main/captive_portal.c:191-198`

---

### MEDIUM: UART Parser State Not Reset on Mode Exit

**Problem:** When exiting BT mode, parser state isn't reset. If UART RX task still running, leftover data could affect next session.

**Impact:** Very unlikely - ESP32 chip stops sending when deactivated, and buffers empty during WiFi usage time.

**File:** `idf_app/main/esp32_comm.c:74-80`

---

### LOW: Zone Resolution Staleness

**Problem:** New zones added to Roon won't appear until next poll cycle (1-30 seconds).

**Impact:** Minor UX delay. User can dismiss and re-open zone picker.

**File:** `common/bridge_client.c:590-591`

---

### LOW: Incomplete HTTP Error Handling

**Problem:** `send_control_json()` returns true if HTTP succeeds but response is empty.

**Impact:** Silent failure of control commands. User gets no feedback.

**File:** `common/bridge_client.c:546-574`

---

## False Positives (Not Actual Issues)

### FP: LVGL Thread Safety Violation

**Analysis:** Zone picker operations ARE on UI thread because:
- `ui_show_zone_picker()` called from `bridge_client_handle_input()`
- `bridge_client_handle_input()` invoked from LVGL event callbacks
- All LVGL event callbacks run on UI thread (`ui_loop_task`)
- Input events processed via `platform_input_process_events()` in same task

**Conclusion:** No thread safety violation - all UI code runs on UI thread.

---

### FP: Zone Picker Visibility Race

**Analysis:** Zone picker show/hide both run on UI thread (see above).

**Conclusion:** No race condition - single-threaded access.

---

### FP: Exit BT Dialog Thread Safety

**Analysis:** Dialog shown/hidden from:
- `bt_input_handler` → called from LVGL event (UI thread)
- Button callbacks → LVGL events (UI thread)

**Conclusion:** No race condition - single-threaded access.

---

### FP: WiFi Stop Not Synchronized with BT Activate

**Analysis:** `esp_wifi_stop()` and `esp_wifi_deinit()` are synchronous (blocking) calls per ESP-IDF docs. Radio is fully released before next line executes.

**Conclusion:** Synchronization is adequate.

---

### FP: AP Mode Config Race

**Analysis:** `wifi_mgr_reconnect()` sets `s_cfg` before calling `wifi_mgr_stop_ap()`, and the STA_START event handler reads `s_cfg` after WiFi restarts. The assignment is complete before the event fires.

**Conclusion:** No race - sequential execution within single task.

---

## Recommendations

### For This Release (v1.3.0)

1. ✅ Double BT activation fixed
2. Test scripts added for manual verification
3. Known issues documented but not blocking

### For Future Releases

1. Add `esp32_comm_deinit()` call when exiting BT mode
2. Use atomic for `s_force_artwork_refresh` flag
3. Improve HTTP error handling with retries
4. Add UART parser reset on mode transitions

---

## Test Coverage

The test scripts in this directory cover the happy paths. Edge cases identified:

| Edge Case | Coverage | Risk |
|-----------|----------|------|
| Mode switch during zone picker open | Not tested | LOW |
| WiFi disconnect during BT switch | Not tested | LOW |
| Rapid mode toggle | Not tested | MEDIUM |
| BT device disconnect during exit | Not tested | LOW |

Consider adding stress tests for mode switching in future releases.
