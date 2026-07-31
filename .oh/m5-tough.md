# Session: m5-tough

## Aim
**Updated:** 2026-07-31

**Aim:** An owner controls their hi-fi system (transport, volume, zone selection) from a physical M5Stack Tough unit sitting on a shelf, instead of using only a phone/tablet app.

**Current State:** M5Stack Tough owners have no HiPhi firmware option; only Dial (ESP32-S3 round) and Frame (ESP32-S3 e-ink) exist.
**Desired State:** An owner flashes verified Tough firmware, provisions Wi-Fi via touch, and drives routine listening (play/pause, volume, zone/source select) without opening a phone app.

### Mechanism
**Change:** A new Tough target/capability profile that composes the existing shared controller/configuration/connectivity/input seams (`common/`, `common/platform/`) behind a touch display driver and touch input bindings appropriate to Tough's verified panel/touch controller, ESP32 (non-S3) memory budget, and partition geometry.
**Hypothesis:** Because playback/config/connectivity logic is already factored out of the Dial/Frame UI layer, Tough only needs its own platform adapter (display/touch/partition/sdkconfig) to become a real Alpha target — not a forked application.
**Assumptions:**
- The owned Tough unit really is the official non-S3 ESP32-D0WDQ6-V3 revision (needs physical board-marking confirmation per #193).
- ESP32 (classic) has enough flash/RAM/no-PSRAM headroom for the shared controller stack once BLE/manifest extras are excluded.
- Wi-Fi-only (no BLE) is an acceptable Alpha scope; BLE HID coexistence on classic ESP32 is unproven and likely deferred.

### Feedback
**Signal:** Exact CI-artifact SHA boots and renders on the physical Tough unit, completes Wi-Fi provisioning, survives reconnect/recovery, and sustains routine transport/volume/zone control against the bridge — reported back by the owner after flashing.
**Timeframe:** Within this delivery slice, gated on physical hardware testing before the PR leaves draft.

### Guardrails
- Do not infer ESP32-S3 assumptions (PSRAM, BLE budget, display/touch parts, partitions, OTA geometry) from the Dial/Frame targets or from the "M5 product family" name — only from verified Tough-specific facts (`#193`).
- Do not fork the Dial application, backend behavior, or LVGL-coupled domain logic — Tough must be a platform composition, not a clone.
- No BLE unless #193 hardware evidence + coexistence testing justify it; default off either way.
- No normal OTA path for this Alpha target.
- Stop and re-verify hardware facts (do not proceed to `/execute` driver work) if physical board markings contradict the assumed ESP32-D0WDQ6-V3 identity.

## Handoff context (from dispatching task)

- Base branch: `codex/issue-190-provisioning-config`
- Base commit: `c78199ca91bafc4e8b9dfbd5bd57b51374346dd5`
- Parent PR: #222
- New branch: `codex/issue-165-m5-tough`
- New PR base: `codex/issue-190-provisioning-config`
- Delivery issue: #165
- Hardware-profile gate: #193
- Epic: #200
- Shared BLE context: #191
- Related: `.oh/input-bindings.md` (issue #194) explicitly excluded Tough from the shared-input work "unless #193 proves the owned hardware is a distinct S3 product/revision" — #165's body has since been updated (today) to explicitly frame Tough as ESP32 non-S3, so that exclusion is now the accepted framing, not a blocker, for *this* slice. Physical board-marking confirmation from the owner is still outstanding.

## Problem Space
**Updated:** 2026-07-31

Posted hardware facts to #193 (comment: https://github.com/muness/roon-knob/issues/193#issuecomment-5147324816).

Key facts (SKU K034, ESP32-D0WDQ6-V3):
- Classic ESP32, 16MB flash, 8MB **Quad**-mode PSRAM (not Dial's octal)
- Display: ILI9342C 320x240 SPI (not Dial's SH8601 QSPI)
- Touch: CHSC6540 I2C 0x2E, GPIO21/22 (not Dial's CST816D)
- No rotary encoder, no physical buttons besides power/reset — touch-only input
- PMU AXP192, RTC BM8563, audio NS4168, USB CH9102
- BT: chip supports Classic+BLE, but shared rk_ble_hid_host (#191) is unverified on classic ESP32 — no coexistence/build evidence exists
- Partition precedent: follow Frame's factory-only (no OTA) shape, not Dial's OTA+octal-PSRAM shape

Unresolved (needs owner's physical unit, not further research): board-marking confirmation that owned unit == documented SKU K034. Does not block design work; must gate physical flash/test and PR-out-of-draft.

Ready for solution-space: yes.

## Problem Statement
**Updated:** 2026-07-31

Posted to #165: https://github.com/muness/roon-knob/issues/165#issuecomment-5147329312

Reframed from "clone Dial for a different screen / revive manifest_ui #132" to "add one more verified target profile composed behind the already-shared controller/config/connectivity seams (#190/#198), using Tough's own primary-source facts (#193), not inherited S3 assumptions." BLE is an independently-gated decision, not assumed in either direction, deferred to /solution-space.

## Solution Space
**Updated:** 2026-07-31

Posted decision to #165: https://github.com/muness/roon-knob/issues/165#issuecomment-5147335687

**Decision: Option A — Wi-Fi + touch Alpha first, BLE explicitly deferred (permanently off, not silently omitted).**
Rejected Option B (capability-gated BLE) for lack of classic-ESP32 build/coexistence evidence on rk_ble_hid_host (#191, S3-only so far). Rejected Option C (larger adaptive/manifest UI) as over-scoped, repeating #193's own already-rejected Phase-1 mistake.

Implementation shape for /execute:
- New Tough app dir/component: ILI9342C display driver (SPI) + CHSC6540 touch driver (I2C 0x2E, GPIO21/22), reusing common/ + common/platform/* unmodified.
- sdkconfig: CONFIG_IDF_TARGET=esp32 (classic), CONFIG_SPIRAM_MODE_QUAD (not Dial's OCT).
- Partition: factory-only (Frame's no-OTA precedent), no ota_0/ota_1.
- Touch->action bindings: minimal local mapping via existing common/controller_action_router.h (play/pause, volume, zone/source select) — not blocked on #194.
- BLE: capability flag permanently off, documented why (points to #193/#191).

## Execute (in progress)
**Updated:** 2026-07-31

Delegated tough_app/ scaffold to background agent (general-purpose), full spec covering:
- tough_app/ directory mirroring frame_app/ conventions (CMakeLists, sdkconfig.defaults, partitions.csv factory-only)
- Display: ILI9342C SPI (MISO38/MOSI23/SCK18/CS5), Touch: CHSC6540 I2C 0x2E (SDA21/SCL22)
- classic esp32 target, CONFIG_SPIRAM_MODE_QUAD (not OCT), BLE explicitly absent
- New touch_ui.c (LVGL, reduced-scope vs Dial's ui.c: text lines, play/pause, +/- volume, zone list, settings)
- controller_presentation_tough.c / controller_input_profile_tough.c (empty descriptors - touch bypasses input-mailbox like Dial) / platform_*_tough.c adapters
- Tests: TEST_TOUGH_PROFILE variant + controller_presentation_tough test, host-cc-compiled only
- CI: build-tough job mirroring build-frame (esp32 target, no BLE assertions), explicitly NOT wired into release/deploy-pages/deploy-pr-preview (scoping decision: no public flash channel until hardware-tested)
- docs/esp/hw-reference/board-tough.md (new file, doesn't touch board.md which is Dial's canonical doc per #211)
- Agent instructed to flag unverifiable hardware guesses as TODO(hardware-verify) rather than fabricate, not to touch common/idf_app/frame_app/shared BLE components, not to run idf.py (no toolchain here) or commit.

Next when agent returns: review full diff, run host-side tests locally with cc, then /review + /dissent, then open draft PR against codex/issue-190-provisioning-config, post review/dissent to PR per required process.

## Execute (completed)
**Updated:** 2026-07-31

The background scaffold covered platform adapters (storage/mdns/log/identity/
provisioning/http/input) and headers, but was missing the actual application
files it referenced (`main_tough.c`, `touch_ui.c`, `platform_display_tough.c`,
`captive_portal.c`/`.h`, `dns_server.c`/`.h`) and `docs/esp/hw-reference/board-tough.md`.
Finished those myself:

- Researched CHSC6540 touch register protocol and the AXP192 power-sequencing
  requirement (LCD/touch RST and backlight are AXP192-mediated, not direct
  ESP32 GPIOs -- not previously captured anywhere in this repo) directly from
  `m5stack/M5GFX`'s open-source driver (Apache-2.0/FreeBSD), read live via
  `gh api` during this session. Documented in `docs/esp/hw-reference/board-tough.md`
  with source attribution. This was necessary to write a display/touch driver
  that has a real chance of working, not just compiling.
- Wrote `platform_display_tough.c` (AXP192 sequencing + ILI9341 SPI panel +
  CHSC6540 I2C touch + LVGL glue), `touch_ui.c` (reduced-scope LVGL UI per
  touch_ui.h's contract), `main_tough.c`, `captive_portal.c`/`.h` (Frame's
  portal minus all BLE routes -- Tough has none), `dns_server.c`/`.h` (copied
  verbatim, target-generic).
- Built successfully for classic ESP32 via local Docker `espressif/idf:v5.5.5`
  (same version Frame/Dial CI use). Required two fixes beyond the scaffold:
  `esp_app_format` PRIV_REQUIRES (esp_app_desc.h use), and
  `idf_build_set_property(MINIMAL_BUILD ON)` in tough_app/CMakeLists.txt --
  without it, ESP-IDF's default "build everything under EXTRA_COMPONENT_DIRS"
  behavior silently pulled in `components/rk_ble_hid_host` (and NimBLE/`bt`)
  even though nothing in tough_app/ requires it. This is the actual
  build-level enforcement of "no BLE component anywhere" for this target, not
  just a convention.
- Verified in the built sdkconfig/compile_commands.json: `CONFIG_IDF_TARGET_ESP32=y`,
  `CONFIG_SPIRAM_MODE_QUAD=y` (no OCT), `CONFIG_COMPILER_OPTIMIZATION_PERF=y`,
  no `CONFIG_BT_ENABLED`/`CONFIG_RK_BLE_HID_HOST`, no `bt`/`rk_ble_hid_host`
  in the compile database.
- Host-side tests: wrote `tests/test_controller_input_profile_tough.c` and
  `tests/test_controller_presentation_tough.c` (new, standalone -- Tough's
  empty physical-input profile and larger touch_ui.h contract don't fit the
  existing DIAL/FRAME-parameterized shared test files) and extended
  `tests/test_platform_provisioning_adapter.c` with a `TEST_TOUGH_ADAPTER`
  branch (Tough's adapter is behaviorally identical to Frame's). Re-ran the
  full existing `test-shared` suite (Dial/Frame provisioning adapter tests,
  controller_config/input/mailbox/action_router/values, identity + controller
  dependency checks) to confirm nothing broke.
- Added `build-tough` to `.github/workflows/docker.yml` (esp32 target, no BLE
  assertions, classic-ESP32-appropriate merge_bin offsets/flash mode)
  uploading an artifact named `hiphi-tough-firmware-unverified`. Deliberately
  NOT added to `release`/`deploy-pr-preview`/`deploy-pages`'s `needs:` --
  verified those three `needs:` lists are unchanged. Ran the exact CI command
  sequence locally end-to-end before committing it.
- Two small shared-file touches beyond tough_app/: added Tough's mdns
  `_roonknob` string to the existing `legacy-mdns-service` identity exception
  (same rule Dial/Frame already use) and reworded this file's own "roon-knob"
  mention to "HiPhi" so `scripts/check_dial_identity.py` passes.

Remaining/blocked: physical hardware validation (#193) -- board-marking
confirmation and actual flash/boot/touch/Wi-Fi test on the owner's unit. This
gates moving the PR out of draft and any public artifact distribution; it is
not something this session can produce. Everything else in the delivery
checklist is done with evidence above.
