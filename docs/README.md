# Documentation

Documentation for the **HiPhi ESP firmware family**. Start at
[targets/README.md](targets/README.md) to find what exists for a particular
controller.

## Quick Links

| Area | Path | Description |
|------|------|-------------|
| [Usage](usage/) | `docs/usage/` | End-user guides: setup, WiFi, OTA |
| [Dev](dev/) | `docs/dev/` | Developer reference: build, boot, FreeRTOS, storage |
| [Targets](targets/) | `docs/targets/` | Per-controller index: what is documented where |
| [Dial](dial/) | `docs/dial/` | Dial and shared ESP32 hardware: display, touch, encoder, battery |
| [Meta](meta/) | `docs/meta/` | Project aims, roadmap, architectural decisions |
| [Howto](howto/) | `docs/howto/` | Tutorials: porting, patterns for reuse |

---

## Usage

Guides for setting up and using a controller.

- [DIAL.md](usage/DIAL.md) - HiPhi Dial quick start, controls, troubleshooting
- [GETTING_STARTED.md](usage/GETTING_STARTED.md) - First-time setup (Docker, flashing, WiFi)
- [WIFI_PROVISIONING.md](usage/WIFI_PROVISIONING.md) - WiFi setup via captive portal
- [OTA_UPDATES.md](usage/OTA_UPDATES.md) - Over-the-air firmware updates

## Dev

Developer reference for building and extending the firmware.

- [DEVELOPMENT.md](dev/DEVELOPMENT.md) - Build setup, PC simulator, bridge development
- [IMPLEMENTATION_NOTES.md](dev/IMPLEMENTATION_NOTES.md) - Architecture overview, input/display systems
- [KIZZ_VOICE.md](dev/KIZZ_VOICE.md) - Kizz wake word, transcription, action path, and observed STT evidence
- [KCONFIG.md](dev/KCONFIG.md) - ESP-IDF configuration options
- [NVS_STORAGE.md](dev/NVS_STORAGE.md) - Non-volatile storage (config persistence)
- [BOOT_SEQUENCE.md](dev/BOOT_SEQUENCE.md) - System initialization flow
- [FREERTOS_PATTERNS.md](dev/FREERTOS_PATTERNS.md) - Threading, tasks, synchronization

### Testing

- [TEST_WIFI_ROON_MODE.md](dev/testing/TEST_WIFI_ROON_MODE.md) - WiFi + bridge integration tests
- [CODE_REVIEW_FINDINGS.md](dev/testing/CODE_REVIEW_FINDINGS.md) - Code review notes

## Targets

- [targets/README.md](targets/README.md) - What is documented for Dial, Frame, Slate, Joy, Tough, and the M5 betas

## Dial

HiPhi Dial hardware and driver documentation. Much of it is shared ESP32
material that other targets also rely on.

- [DISPLAY.md](dial/DISPLAY.md) - SH8601 AMOLED setup, LVGL integration
- [TOUCH_INPUT.md](dial/TOUCH_INPUT.md) - CST816 touch controller
- [SWIPE_GESTURES.md](dial/SWIPE_GESTURES.md) - Gesture detection
- [ROTARY_ENCODER.md](dial/ROTARY_ENCODER.md) - Quadrature encoder handling
- [BATTERY_MONITORING.md](dial/BATTERY_MONITORING.md) - ADC-based battery level
- [FONTS.md](dial/FONTS.md) - LVGL font configuration

### Hardware Reference

Pin mappings, datasheets, component details.

- [board.md](dial/hw-reference/board.md) - Board overview and specs
- [HARDWARE_PINS.md](dial/hw-reference/HARDWARE_PINS.md) - GPIO assignments
- [COLORTEST_HELLOWORLD.md](dial/hw-reference/COLORTEST_HELLOWORLD.md) - Display color format
- [cst816d.md](dial/hw-reference/cst816d.md) - Touch controller details
- [encoder.md](dial/hw-reference/encoder.md) - Rotary encoder interface
- [battery.md](dial/hw-reference/battery.md) - Battery monitoring circuit
- [drv2605.md](dial/hw-reference/drv2605.md) - Haptic motor driver
- [image_render.md](dial/hw-reference/image_render.md) - JPEG decoding
- [now_playing_image.md](dial/hw-reference/now_playing_image.md) - Artwork display
- [touch.md](dial/hw-reference/touch.md) - Touch integration notes

## Meta

Project planning and architectural decisions.

- [PROJECT_AIMS.md](meta/PROJECT_AIMS.md) - Why this project exists, prioritization framework
- [ROADMAP_IDEAS.md](meta/ROADMAP_IDEAS.md) - User feedback and feature ideas

### Decisions

Date-prefixed architectural decision records.

- [2025-12-20_DESIGN_KNOB_CONFIG.md](meta/decisions/2025-12-20_DESIGN_KNOB_CONFIG.md) - Per-knob config from bridge
- [2025-12-20_DECISION_ROTATION.md](meta/decisions/2025-12-20_DECISION_ROTATION.md) - Display rotation (0/180 only)
- [2025-12-20_DECISION_JSON_PARSING.md](meta/decisions/2025-12-20_DECISION_JSON_PARSING.md) - cJSON for API parsing

### Analysis

Competitor and alternative project analysis.

- [blueknob-analysis.md](meta/analysis/blueknob-analysis.md) - BlueKnob project analysis
- [smart-knob-analysis.md](meta/analysis/smart-knob-analysis.md) - Smart Knob project analysis

## Howto

Tutorials for porting and reusing patterns.

- [PORTING.md](howto/PORTING.md) - Porting to other ESP32 boards or building different apps
- [AtomS3 Joystick hardware](dial/hw-reference/board-atom-s3-joystick.md)

## M5 hardware and Wi-Fi setup

- [M5Stack Tough build, flash, and controls](dial/M5STACK.md)
- [Wi-Fi scan behavior and troubleshooting](dial/WIFI_SCAN.md)
