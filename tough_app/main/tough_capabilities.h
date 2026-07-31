#pragma once

// M5Stack Tough (SKU K034) target capability/identity declaration.
//
// This repo has no pre-existing generic "capability" or "target profile"
// struct/enum in common/ to plug into (checked: common/*.h, common/platform/*.h
// before adding this). Rather than build speculative infrastructure ahead of
// need, this header keeps the "no BLE, touch-only" facts explicit and
// greppable for this Alpha slice. If a future target needs a real capability
// registry, promote this into a shared struct at that point.
//
// See docs/esp/hw-reference/board-tough.md for sourcing of these facts.

// Bluetooth (BLE and Classic) is permanently OFF for this Alpha slice,
// pending #193/#191 classic-ESP32 Wi-Fi/BT coexistence evidence. No BLE
// component, Kconfig, or code is referenced anywhere under tough_app/.
#define TOUGH_CAPABILITY_BLE_SUPPORTED 0

// No rotary encoder is present on Tough hardware.
#define TOUGH_CAPABILITY_HAS_ENCODER 0

// No software-visible physical buttons. Power/reset are hardware-only per
// M5Stack docs and are not GPIO-routable to firmware.
#define TOUGH_CAPABILITY_HAS_PHYSICAL_BUTTONS 0

// CHSC6540 capacitive touch panel is present and is the sole input surface.
#define TOUGH_CAPABILITY_TOUCH_SUPPORTED 1
