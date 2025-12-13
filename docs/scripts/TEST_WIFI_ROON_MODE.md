# Test Script: WiFi/Roon Mode

Happy path test script for WiFi setup and Roon control functionality.

## Prerequisites

- Fresh firmware flash (both ESP32-S3 and ESP32)
- Roon Core running on local network
- roon-web-controller bridge running (e.g., `http://192.168.1.x:8088`)
- Phone/tablet for WiFi provisioning

## Test Case 1: Initial Setup via Captive Portal

### Steps

1. **Power on device with no saved WiFi credentials**
   - Device should boot and show "Setup: roon-knob-setup" message
   - Display shows setup instructions

2. **Connect phone to "roon-knob-setup" WiFi network**
   - Network should appear within 30 seconds of boot
   - No password required (open network)

3. **Captive portal should auto-open**
   - If not, navigate to `http://192.168.4.1/`
   - Should see "Roon Knob - WiFi Setup" form

4. **Enter WiFi credentials**
   - SSID: Your home WiFi network name
   - Password: Your WiFi password
   - Bridge URL: (optional) `http://192.168.1.x:8088`
   - Leave bridge empty to use mDNS auto-discovery

5. **Submit form**
   - Should see "WiFi credentials saved!" success message
   - Device reboots within 2 seconds

### Expected Results

- [ ] Captive portal form renders correctly
- [ ] Form accepts SSID, password, and optional bridge URL
- [ ] Success message displays after submission
- [ ] Device reboots automatically
- [ ] "roon-knob-setup" network disappears

---

## Test Case 2: WiFi Connection After Setup

### Steps

1. **Device boots with saved credentials**
   - Display shows "WiFi: Connecting..."

2. **Wait for connection (5-30 seconds)**
   - Display updates to "WiFi: Connected"
   - If bridge configured: "Bridge: Testing..."
   - Then: "Bridge: Connected" or "Bridge: Searching..."

3. **mDNS discovery (if no bridge URL configured)**
   - Display shows "Bridge: Searching..."
   - Once found: "Bridge: Found"

### Expected Results

- [ ] WiFi connects within 30 seconds
- [ ] IP address obtained (visible in logs)
- [ ] Bridge discovered via mDNS or connects to configured URL
- [ ] No "WiFi: Retrying..." loops (unless credentials wrong)

---

## Test Case 3: Zone Selection on First Boot

### Steps

1. **After bridge connects, tap the zone label**
   - Zone label initially shows "Press knob to select zone"
   - Or shows last selected zone name

2. **Zone picker appears**
   - Roller shows available Roon zones
   - "Bluetooth" option at bottom (if enabled)

3. **Scroll to desired zone using rotary encoder**
   - Clockwise = scroll down
   - Counter-clockwise = scroll up

4. **Select zone by pressing the knob (center button)**
   - Zone picker closes
   - Zone label updates to selected zone name
   - Display shows "Loading zone..."

### Expected Results

- [ ] Zone picker shows all available Roon zones
- [ ] Scrolling works smoothly
- [ ] Zone selection persists after reboot
- [ ] Zone picker closes after selection
- [ ] "Bluetooth" appears as last option

---

## Test Case 4: Now Playing Display

### Steps

1. **Play music on selected Roon zone**
   - Use Roon app or another controller

2. **Verify display updates**
   - Line 1: Track title
   - Line 2: Artist name
   - Album artwork (if available)
   - Progress bar showing position/duration
   - Volume indicator

3. **Change track in Roon**
   - Display should update within 1-2 seconds

4. **Verify play state indicator**
   - Play icon when playing
   - Pause icon when paused

### Expected Results

- [ ] Track title displays correctly (truncates if too long)
- [ ] Artist name displays correctly
- [ ] Album artwork loads and displays
- [ ] Progress bar updates in real-time
- [ ] Volume level matches Roon zone volume
- [ ] Play/pause state indicator is accurate

---

## Test Case 5: Playback Controls

### Steps

1. **Volume control via rotary encoder**
   - Rotate clockwise: Volume up
   - Rotate counter-clockwise: Volume down
   - Volume overlay appears briefly showing new level

2. **Play/Pause via center button press**
   - Short press toggles play/pause
   - Verify state change on display and in Roon

3. **Next track via right swipe**
   - Swipe right on touchscreen
   - Track advances to next in queue

4. **Previous track via left swipe**
   - Swipe left on touchscreen
   - Track goes to previous (or restarts if >3s in)

### Expected Results

- [ ] Volume changes reflected in Roon within 500ms
- [ ] Volume overlay appears on adjustment
- [ ] Play/pause toggles correctly
- [ ] Next/previous track work
- [ ] Controls feel responsive (no lag)

---

## Test Case 6: Display Sleep/Wake

### Steps

1. **Wait for display to sleep (default: 30 seconds of inactivity)**
   - Display dims and turns off
   - Album artwork may remain visible (art mode)

2. **Wake display**
   - Rotate encoder, or
   - Touch screen, or
   - Press center button

3. **Verify controls still work when display is sleeping**
   - Volume changes should still work
   - Play/pause should still work

### Expected Results

- [ ] Display sleeps after timeout
- [ ] Any input wakes display
- [ ] Controls remain functional during sleep
- [ ] Wake is instant (no delay)

---

## Test Case 7: Connection Recovery

### Steps

1. **Disconnect WiFi (turn off router briefly)**
   - Display shows "WiFi: Retrying..."
   - Or "Bridge: Offline"

2. **Restore WiFi**
   - Device should reconnect automatically
   - Display shows "WiFi: Connected" then "Bridge: Connected"

3. **Restart Roon bridge**
   - Device should rediscover via mDNS
   - Or reconnect to configured URL

### Expected Results

- [ ] Automatic WiFi reconnection
- [ ] Automatic bridge reconnection
- [ ] No manual intervention required
- [ ] Exponential backoff prevents spam

---

## Failure Modes to Verify

- [ ] Wrong WiFi password: Shows "WiFi: Retrying..." then falls back to AP mode after 5 failures
- [ ] Bridge unreachable: Shows "Bridge: Offline", keeps retrying
- [ ] Zone unavailable: Zone picker shows other zones, allows reselection
- [ ] Network latency: Controls should queue and execute in order

---

## Log Verification

Monitor serial output for:
```
WiFi: Connecting...
WiFi connected with IP: 192.168.x.x
mDNS discovered bridge: http://192.168.x.x:8088
Bridge: Connected
```

No errors like:
- `HTTP request failed`
- `CRC mismatch`
- `Stack overflow`
