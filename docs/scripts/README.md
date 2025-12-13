# Test Scripts

Manual test scripts for verifying Roon Knob functionality on new releases.

## Available Scripts

| Script | Description |
|--------|-------------|
| [TEST_WIFI_ROON_MODE.md](TEST_WIFI_ROON_MODE.md) | WiFi setup, Roon control, now playing display |
| [TEST_BLUETOOTH_MODE.md](TEST_BLUETOOTH_MODE.md) | Bluetooth pairing, BLE HID controls, AVRCP metadata |

## Quick Checklist

### Pre-Release Verification

#### WiFi/Roon Mode
- [ ] Fresh setup via captive portal works
- [ ] WiFi connects after reboot
- [ ] Zone picker shows zones + Bluetooth option
- [ ] Now playing displays: title, artist, artwork, progress, volume
- [ ] Controls work: volume, play/pause, next/prev
- [ ] Display sleep/wake works

#### Bluetooth Mode
- [ ] Can select Bluetooth from zone picker
- [ ] WiFi stops, BT activates
- [ ] BLE HID appears in phone's Bluetooth scan
- [ ] Pairing works (no PIN)
- [ ] Controls work: volume, play/pause, next/prev
- [ ] AVRCP metadata displays (phones)
- [ ] Exit dialog works (not zone picker)
- [ ] Mode persists across reboot

### Test Devices

Recommended test matrix:

| Device | WiFi/Roon | BT Controls | BT Metadata |
|--------|-----------|-------------|-------------|
| iPhone | ✅ | ✅ | ✅ |
| Android | ✅ | ✅ | ✅ |
| DAP | N/A | ✅ | ❌ |

## Running Tests

1. Flash both firmware images:
   ```bash
   # ESP32-S3 (normal USB orientation)
   cd idf_app && idf.py flash

   # ESP32 (flip USB-C connector)
   cd esp32_bt && idf.py flash
   ```

2. Open serial monitor for logs:
   ```bash
   idf.py monitor
   ```

3. Follow test scripts, checking off items as you verify

## Reporting Issues

If a test fails:
1. Note the test case number and step
2. Capture serial logs
3. Create a bead: `bd create "Bug: <description>" -t bug`
