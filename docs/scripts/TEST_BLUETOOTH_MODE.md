# Test Script: Bluetooth Mode

Happy path test script for Bluetooth media control functionality.

## Prerequisites

- Firmware flashed on both chips:
  - ESP32-S3: `idf_app/` (display, touch, mode switching)
  - ESP32: `esp32_bt/` (BLE HID + AVRCP)
- A Bluetooth-capable device to pair:
  - iPhone (best - full BLE HID + AVRCP support)
  - Android phone (full support)
  - DAP/music player (BLE HID only, no metadata)

## Architecture Overview

```
┌─────────────────┐     UART      ┌─────────────────┐
│   ESP32-S3      │◄────────────►│     ESP32       │
│                 │   1Mbps       │                 │
│ - Display/Touch │               │ - BLE HID       │
│ - WiFi (off)    │               │ - BT AVRCP      │
│ - Mode control  │               │ - Bonding       │
└─────────────────┘               └─────────────────┘
                                         │
                                         │ Bluetooth
                                         ▼
                                  ┌─────────────────┐
                                  │  Phone/DAP      │
                                  │                 │
                                  │ - Media player  │
                                  │ - Volume        │
                                  └─────────────────┘
```

---

## Test Case 1: Enter Bluetooth Mode

### Steps

1. **Start in WiFi/Roon mode**
   - Device connected to WiFi
   - Displaying Roon now playing (or zone selection)

2. **Tap zone label to open zone picker**
   - Zone picker overlay appears
   - Shows available Roon zones
   - "Bluetooth" option at bottom

3. **Scroll to "Bluetooth" and select**
   - Use rotary encoder to scroll down
   - Press center button to select

4. **Verify mode switch**
   - Zone picker closes immediately
   - Zone label changes to "🔵 Bluetooth"
   - Display shows "Bluetooth" / "Waiting for track..."

### Expected Results

- [ ] Zone picker closes promptly (no overlay stuck)
- [ ] WiFi stops (verify in logs: "Stopping WiFi for Bluetooth mode")
- [ ] BT activated (verify: "Activating Bluetooth on ESP32")
- [ ] UI switches to Bluetooth mode visuals
- [ ] ESP32 receives CMD_BT_ACTIVATE

---

## Test Case 2: BLE HID Pairing

### Steps

1. **On your phone, open Bluetooth settings**
   - Settings > Bluetooth (iOS)
   - Settings > Connected devices > Pair new device (Android)

2. **Look for "Roon Knob" in available devices**
   - Should appear within 5-10 seconds
   - Device is advertising BLE HID

3. **Tap to pair**
   - "Just works" pairing (no PIN required)
   - Should connect immediately

4. **Verify connection**
   - Phone shows "Connected" for Roon Knob
   - Knob display may update (device name shown via AVRCP)

### Expected Results

- [ ] "Roon Knob" appears in Bluetooth scan
- [ ] Pairing completes without PIN prompt
- [ ] Connection established quickly (<5 seconds)
- [ ] Bond saved (will auto-reconnect after reboot)

---

## Test Case 3: BLE HID Controls

### Steps

1. **Open a music app on your phone**
   - Apple Music, Spotify, YouTube Music, etc.
   - Start playing a track

2. **Test volume control**
   - Rotate encoder clockwise: Volume up
   - Rotate encoder counter-clockwise: Volume down
   - Phone volume should change immediately

3. **Test play/pause**
   - Press center button: Toggle play/pause
   - Verify music pauses/resumes on phone

4. **Test next/previous track**
   - Swipe right on touchscreen: Next track
   - Swipe left on touchscreen: Previous track
   - Verify track changes on phone

### Expected Results

- [ ] Volume control works across all apps (system volume)
- [ ] Play/pause works in music apps
- [ ] Next/previous works in music apps
- [ ] Controls feel responsive (<100ms latency)
- [ ] Works even when phone is locked

---

## Test Case 4: AVRCP Metadata Display

### Steps

1. **Play music on paired phone**
   - Use any music app

2. **Verify metadata appears on display**
   - Line 1: Track title
   - Line 2: Artist name
   - Play/pause state indicator

3. **Change tracks on phone**
   - Metadata should update within 1-2 seconds

4. **Check position/duration (if supported)**
   - Progress bar may show position
   - Not all devices support this

### Expected Results

- [ ] Track title displays correctly
- [ ] Artist name displays correctly
- [ ] Metadata updates on track change
- [ ] Play state indicator matches actual state

### Device Compatibility Notes

| Device | BLE HID Controls | AVRCP Metadata |
|--------|-----------------|----------------|
| iPhone | ✅ Works | ✅ Works |
| Android | ✅ Works | ✅ Works |
| DAPs (KANN Ultra, etc.) | ✅ Works | ❌ No* |

*DAPs typically require A2DP sink for AVRCP. Our device is AVRCP controller only.

---

## Test Case 5: Exit Bluetooth Mode

### Steps

1. **Tap zone label while in Bluetooth mode**
   - "Exit Bluetooth?" dialog appears
   - Two buttons: "Exit" and "Cancel"

2. **Test Cancel**
   - Tap "Cancel"
   - Dialog closes, stays in Bluetooth mode

3. **Test Exit**
   - Tap zone label again
   - Tap "Exit" button
   - Mode switches back to Roon

4. **Verify mode switch**
   - Bluetooth deactivated on ESP32
   - WiFi starts
   - Zone label shows last Roon zone

### Expected Results

- [ ] Exit dialog appears (not full zone picker)
- [ ] Cancel keeps you in Bluetooth mode
- [ ] Exit switches to Roon mode
- [ ] WiFi reconnects automatically
- [ ] ESP32 receives CMD_BT_DEACTIVATE

---

## Test Case 6: Bluetooth Mode Persistence

### Steps

1. **Enter Bluetooth mode and pair with phone**

2. **Reboot the device**
   - Power cycle or reset

3. **Verify on boot**
   - Device starts in Bluetooth mode
   - Automatically sends BT activate command
   - Phone should auto-reconnect (bonded)

4. **Exit and reboot again**
   - Exit to Roon mode
   - Reboot
   - Device should start in Roon mode

### Expected Results

- [ ] Mode persists across reboots
- [ ] BT activation sent on boot if in BT mode
- [ ] Phone auto-reconnects (bond preserved)
- [ ] Mode state saved to NVS

---

## Test Case 7: BLE HID Without AVRCP

### Steps

1. **Connect a device that doesn't support AVRCP**
   - Some DAPs, older devices
   - Or manually disconnect AVRCP on phone

2. **Verify controls still work**
   - Volume up/down
   - Play/pause
   - Next/previous

3. **Verify display shows fallback**
   - "Bluetooth" as title
   - "Waiting for track..." or "Connected" as artist

### Expected Results

- [ ] BLE HID controls work independently of AVRCP
- [ ] Display shows sensible fallback text
- [ ] No crashes or errors

---

## Test Case 8: Reconnection After Disconnect

### Steps

1. **While connected, disable Bluetooth on phone**
   - Or move phone out of range

2. **Verify display updates**
   - Shows disconnected state
   - Or "Waiting for connection..."

3. **Re-enable Bluetooth on phone**
   - Phone should auto-reconnect
   - Display updates with metadata

### Expected Results

- [ ] Graceful handling of disconnect
- [ ] Automatic reconnection
- [ ] Metadata resumes after reconnect

---

## Failure Modes to Verify

- [ ] Phone Bluetooth off: Shows waiting state, reconnects when enabled
- [ ] Out of range: Disconnects gracefully, reconnects when in range
- [ ] AVRCP disconnect only: Controls still work via BLE HID
- [ ] Multiple pairing attempts: Should handle gracefully

---

## Log Verification (ESP32-S3)

Monitor serial output for:
```
Controller mode changed to: Bluetooth
Stopping WiFi for Bluetooth mode
Activating Bluetooth on ESP32
Sending BT_ACTIVATE command
```

When exiting:
```
User confirmed exit from Bluetooth mode
Deactivating Bluetooth on ESP32
Starting WiFi for Roon mode
```

## Log Verification (ESP32)

Monitor serial output for:
```
Received message: type=0x13, len=0  (CMD_BT_ACTIVATE)
Activating Bluetooth...
BT state: 1  (DISCOVERABLE)
BT state: 3  (CONNECTED)
Metadata[1]: Track Title
Metadata[2]: Artist Name
```

---

## Troubleshooting

### "Roon Knob" doesn't appear in Bluetooth scan

1. Verify ESP32 firmware is flashed (flip USB-C)
2. Check ESP32 serial logs for BT initialization
3. Ensure device is in Bluetooth mode (not Roon mode)
4. Try: Exit BT mode, re-enter to restart advertising

### Controls work but no metadata

1. This is expected for DAPs and some devices
2. Verify AVRCP connection in ESP32 logs
3. Some apps don't expose metadata via AVRCP

### Volume controls phone but not media app

1. BLE HID controls system volume
2. Some apps have separate volume (try system volume)
3. This is expected behavior
