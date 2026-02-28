# Testing BT Controller Modes

## What's Been Implemented

Three Bluetooth operating modes on the ESP32:

1. **Phone Mode** (default): BLE HID for controls + AVRCP for metadata
   - Works with phones (current behavior)
   - BLE "Knob control" + Classic BT "Knob info"
   
2. **Controller Mode**: AVRCP passthrough only + A2DP SDP hack
   - For Tesla/DAPs that only accept one BT device
   - Uses AVRCP commands for play/pause/next/prev/volume
   - A2DP SDP record deleted to prevent audio routing
   
3. **Controller No-Hack Mode**: AVRCP passthrough without SDP hack
   - For testing if the SDP hack is needed
   - A2DP service remains visible

## Build and Flash

### ESP32 (Bluetooth chip):
```bash
cd esp32_bt
rm sdkconfig  # Important: Regenerate from defaults
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbserial-XXX flash monitor
```

### ESP32-S3 (Display chip):
```bash
cd esp_dial
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem-XXX flash monitor
```

## Testing via Serial Monitor

Since UI isn't wired up yet, test via ESP32-S3 serial monitor using UART commands:

### Get Current Mode
```c
// S3 sends: CMD_BT_GET_MODE (0x16)
// ESP32 responds: EVT_BT_MODE (0x26) + mode byte
```

### Set Mode to Controller
```c
// S3 sends: CMD_BT_SET_MODE (0x15) + payload: 0x01
// ESP32 switches to controller mode (AVRCP passthrough)
```

### Mode Values
- `0x00` = Phone mode
- `0x01` = Controller mode (with SDP hack)
- `0x02` = Controller mode (no SDP hack)

## Test Plan

### Test 1: Phone Mode (Baseline)
1. Flash both chips
2. Pair phone with both:
   - "Knob control" (BLE HID)
   - "Knob info" (Classic BT)
3. Verify controls work via BLE HID
4. Verify metadata appears from AVRCP

### Test 2: Controller Mode (Tesla/DAP)
1. Switch ESP32 to controller mode
2. Reconnect (may need to unpair/repair)
3. Pair Tesla/DAP with "Roon Knob"
4. Test controls (should use AVRCP passthrough now)
5. Verify A2DP doesn't appear in service list

### Test 3: Controller No-Hack (Debug)
1. Switch to mode 0x02
2. Check if DAP tries to route audio
3. Compare with mode 0x01 behavior

## Expected Logs

### Phone Mode (BLE HID)
```
I (xxx) bt_avrcp: Play via BLE HID (phone mode)
I (xxx) ble_hid_vol: Sending BLE HID play
```

### Controller Mode (AVRCP)
```
I (xxx) bt_avrcp: Play via AVRCP passthrough (controller mode)
I (xxx) bt_avrcp: Sending passthrough cmd 0x44 (tl=0)
```

### Power Management
```
I (xxx) pm: Frequency switching config: CPU_MAX: 80, APB_MAX: 80, APB_MIN: 40, Light sleep: ENABLED
```

## Known Issues / Questions

1. **Deep sleep on deactivate**: ESP32 enters deep sleep when BT deactivated - needs testing if UART wakeup works reliably
2. **80MHz CPU**: May affect BT timing - watch for connection drops or audio glitches
3. **Mode switching**: Requires disconnect/reconnect - not seamless

## Next Steps

1. Test each mode with your devices
2. Add UI selector (zone picker menu) if modes work
3. Adjust power settings if 80MHz causes issues
4. Remove deep sleep if UART wakeup is unreliable
