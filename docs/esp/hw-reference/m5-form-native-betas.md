# Form-native M5 alpha targets

These profiles are deliberately separate firmware artifacts. M5Unified detects
the physical board at boot, and `m5_platform` rejects a binary whose compiled
profile does not match that board.

| Web alpha | Exact product | Flash / PSRAM profile | Hero interaction | Basic controls |
|---|---|---|---|---|
| M5 Dial Lab | M5 Dial K130 | 8 MB / none | The round face is a live volume arc driven by the encoder | Press: play/pause; touch: room picker; turn/press: navigate/select |
| StickS3 Twist Remote | M5StickS3 K150 | 8 MB / 8 MB OPI | Hold the device button and twist the handheld itself for volume | Secondary click: play/pause; secondary hold: rooms; primary/secondary: navigate/select |
| StopWatch Wrist Remote | M5Stack StopWatch C152 | 16 MB / 8 MB OPI | Raise-to-wake AMOLED plus haptic acknowledgement | Edge buttons: volume; tap: play/pause; hold: rooms |
| Kizz Companion | M5StackChan K151 | 16 MB / 8 MB Quad | A Kismet-inspired expressive face can celebrate new tracks and droop once when the connection is lost | Tap: play/pause; hold: body language on/off; swipe up: rooms; swipe/tap: navigate/select |

## Evidence level

The profiles compile with ESP-IDF 5.5.5, M5Unified 0.2.19, and the resolved
M5GFX 0.2.27 component. CI creates four-part ESP Web Tools manifests and checks
their bootloader, partition table, OTA data, application offsets, and assets.

That evidence establishes build and web-flash readiness, not physical hardware
behavior. Until an immutable PR-preview artifact passes flash, sustained boot,
display, input, Wi-Fi provisioning, and control checks on the exact product,
the preview pages and PR remain labeled **hardware-unverified alpha**.

Kizz body language is off by default and persisted only after an explicit
hold on the face. A new track triggers a short two-step dance; losing an active
connection triggers one sad droop. Each gesture powers the servo rail briefly,
reads both current positions, moves within a 64-step guard band around that
observed neutral pose, returns to neutral, and powers the rail off. A missing or
invalid servo response disables and forgets the option. The driver follows the
official K151 BSP's IO-expander address, UART pins, servo IDs, protocol, and raw
limits, but the gestures remain hardware-unverified until the immutable preview
artifact passes exact-unit motion checks.
