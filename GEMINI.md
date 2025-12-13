# Notes for AI Assistants

## Important: Branch Before Making Major Changes

BLE HID mode development was done directly on master (2025-12-10). This should have been on a feature branch.

**Lesson learned**: Always create a feature branch before starting significant new features:

```bash
git checkout -b feature/ble-hid-mode
```

The master branch may have unstable BLE code that needs cleanup.

## Current BLE HID Status (2025-12-10)

- BLE HID mode partially implemented
- Memory issues when running WiFi + BT together (both need significant RAM)
- WiFi must be stopped before starting BLE
- Device name still showing old cached value on some devices
- Play/pause works, next/prev track needs testing
