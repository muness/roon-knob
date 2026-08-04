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
- Two small shared-file touches beyond tough_app/: added Tough's mDNS
  compatibility identifier to the existing `legacy-mdns-service` identity
  exception (the same rule Dial/Frame already use), then reworded this
  session note so `scripts/check_dial_identity.py` passes.

Remaining/blocked: physical hardware validation (#193) -- board-marking
confirmation and actual flash/boot/touch/Wi-Fi test on the owner's unit. This
gates moving the PR out of draft and any public artifact distribution; it is
not something this session can produce. Everything else in the delivery
checklist is done with evidence above.

## Solution Space (superseding the 2026-07-31 decision)
**Updated:** 2026-08-04

### Delivery Topology

- One delivery issue: #165.
- One draft implementation PR: #224.
- Architecture note, risk-retirement work, shared extraction, Tough migration,
  first-run Wi-Fi scan/select, tests, and physical acceptance all land through
  that PR. Do not create child issues or stacked PRs for this port.
- Related epics/issues are context and dependency evidence only; they do not
  split delivery ownership away from #165/#224.

### Solution Space Analysis

**Problem:** Deliver a correct Tough port that becomes the first reusable M5
family implementation, rather than preserving a hand-written Tough clone.

**Key Constraint:** M5 board behavior must be owned by pinned M5Unified/M5GFX,
while product behavior remains in the shared controller, configuration,
connectivity, provisioning, recovery, and semantic presentation/input layers.

**Working Story:** A single SoC-specific `m5_app` composition can reuse a narrow
C-facing `m5_platform` component and explicit per-model profiles. M5Unified
performs supported-board initialization/detection; HiPhi's allowlist decides
whether an exact model is qualified and which capabilities/presentation/input
profile it exposes. M5GFX directly owns M5 rendering. Wi-Fi scan/select and
credential submission are shared provisioning semantics consumed by either an
on-device renderer or the captive portal.

**Success Signal:** The exact CI Tough artifact identifies only the qualified
K034 profile, starts first-run setup by scanning and showing selectable nearby
SSIDs before credential entry, provisions and
reconnects without reset, sustains playback control, and records stable internal
RAM/stack headroom. Adding a second M5 model requires a profile, target config,
and renderer/input specialization only--not copied controller, portal,
provisioning, PMIC, display, or touch code.

**Decision Criteria:** Vendor ownership of board behavior; shared-product
boundary integrity; exact-model safety; provisioning UX; internal-RAM safety;
cost of adding a second M5 device; incremental deliverability.

**Critical Assumptions:** Pinned M5Unified 0.2.19 and M5GFX 0.2.26 build under
ESP-IDF 5.5.5 for classic ESP32; their Tough detection and display/touch/PMIC
initialization work on the owned K034; a direct M5GFX renderer avoids LVGL
without losing required controller behavior; APSTA scanning can keep the setup
service available; the classic ESP32 internal-memory budget is sufficient when
all flash-capable task stacks are forced internal.

### Candidates Considered

| Option | Level | Approach | Main Trade-off |
|---|---|---|---|
| A | Band-Aid | Keep the spike and only fix the NVS/cache-off reset plus scan UI | Fast, but preserves hand-copied M5 drivers, LVGL coupling, duplicated portal code, and a one-off app |
| B | Local Optimum | Replace Tough's hand drivers with M5Unified/M5GFX but keep `tough_app` and LVGL | Corrects vendor ownership, but the next M5 device still clones composition/UI and creates dual display ownership |
| C | Redesign | One SoC-specific M5 app composition, C-facing M5 platform component, explicit model profiles, direct M5GFX presentation, and shared provisioning scan contract | More factoring now and a C/C++ boundary, but creates the intended path for future M5 devices |
| D | Reframe | First build a universal adaptive UI/provisioning runtime for every target, then add M5 | Broadest reuse, but blocks Tough on unfinished cross-target architecture and exceeds the incremental slice |

### Interpretive Variety Check

- Same frame or different frames? A and B assume Tough is the unit of delivery;
  C treats M5 as the reusable hardware family; D treats all targets as one UI
  runtime.
- Current-frame test: B is the strongest test of the old "Tough app plus shared
  core" frame.
- Failure would teach: If C cannot meet toolchain, memory, or exact-board gates,
  it routes to a bounded Tough-only M5Unified adapter (B), not back to copied
  register code. If shared provisioning cannot remain target-neutral, revisit
  the provisioning problem statement rather than embedding scan policy in the
  M5 UI.

### Risk Retirement Plan

| Risk / Assumption / Alternate Frame | Planned Disposition | Tempting Patch This Must Fail | Required Evidence or Rationale | Stop/Pivot If |
|---|---|---|---|---|
| M5Unified/M5GFX support ESP-IDF 5.5.5 and K034 | Retired by evidence | A compile-only shim or copied M5 register sequence | Pin exact component versions; build a minimal probe and the full app; on hardware assert `M5.getBoard()==board_M5Tough`, display dimensions, touch events, and PMIC/backlight behavior | Probe fails or board is unknown/misidentified |
| One generic M5 artifact is safe | Triggered | Accept every model M5Unified detects | Build artifacts remain SoC-specific and compare detected board to an explicit compiled allowlist/profile before starting product services | An unqualified model reaches Wi-Fi/bridge startup |
| Direct M5GFX presentation preserves the product boundary | Retired by evidence | Calling controller/bridge APIs from M5 widgets | Dependency check plus host contract tests: renderer consumes copied presentation state and emits semantic actions only | M5 UI includes bridge/config internals or invents parallel actions |
| Removing LVGL is safe and cheaper | Retired by evidence | Keeping LVGL as an unused compatibility layer or using M5GFX only as an LVGL flush driver | Binary/map comparison and hardware UI acceptance with no LVGL dependency in the M5 build | Required behavior cannot be expressed cleanly or memory savings do not materialize |
| Wi-Fi scan/select can coexist with setup AP | Retired by evidence | Static SSID text field or AP-only portal labeled as scan | APSTA hardware test: repeated scans, selectable SSIDs, password submission, failure/retry, and portal reachability throughout | Scan tears down the portal, starves control work, or causes reconnect loops |
| Flash/NVS work is cache-safe | Retired by evidence | Fixing only Tough's current UI task | Enumerate every flash/NVS call path and assert its executing stack is internal; stress repeated zone/config/Wi-Fi commits while rendering and networking | Any cache-off assertion/reset or unbounded internal-stack allocation remains |
| A family layer really reduces the second-port cost | Retired by evidence | Renaming Tough files to `m5_*` while retaining model conditionals everywhere | Add a host-only second profile fixture and require no copied app/provisioning/platform implementation | A second model needs copied controller, portal, PMIC/display/touch, or Wi-Fi code |
| BLE is needed in this slice | Accepted with rationale | Enabling BLE because the SoC supports it | Out of scope until classic-ESP32 build, memory, and Wi-Fi coexistence evidence exists under #191/#199 | Product acceptance explicitly requires BLE |

### Recommendation

**Selected:** Option C - reusable M5 family port

**Level:** Redesign

This is the lowest option that fixes both observed failures: incorrect ownership
of M5 hardware behavior and the repeated creation of target-local copies. It is
bounded by keeping the established shared controller contracts and by proving
only Tough on hardware in this slice. Future models gain a path, not an
unverified support claim.

**Accepted trade-offs:**

- The M5 platform/presentation implementation is C++ internally and exposes a
  narrow C ABI to the existing C core.
- M5 builds use M5GFX directly rather than sharing LVGL widgets with Dial.
- Artifacts and sdkconfig remain SoC-specific even though M5 source components
  and model profiles are shared.
- Tough remains Wi-Fi-only and outside public release channels until exact-CI-
  artifact hardware acceptance passes.

### Execution Handoff

- Preserve: shared controller/config/connectivity/recovery semantics, semantic
  presentation/input contracts, exact-model hardware claims, and draft-only
  artifact status.
- Verify via: pinned dependency probe; dependency-policy tests; second-profile
  fixture; APSTA scan/provision/recovery tests; cache-off persistence stress;
  exact CI artifact flash and sustained hardware run.
- Decision criteria: vendor board ownership, model safety, shared boundary
  integrity, RAM safety, provisioning UX, and second-M5 incremental cost.
- Critical assumptions: exact dependency/toolchain compatibility, K034 runtime
  detection, direct-M5GFX adequacy, APSTA stability, and internal RAM headroom.
- Accepted trade-offs: C++ adapter, direct M5GFX renderer, SoC-specific artifacts,
  BLE deferred.
- Risk retirement checks: every row in the plan above is mandatory; compile-only,
  static-SSID, single-task stack, and renamed-one-off implementations must fail.
- Invalidated if: M5Unified cannot reliably initialize/detect K034 on the pinned
  toolchain; the direct renderer cannot preserve semantic boundaries; or the
  required runtime cannot fit with measured internal-memory headroom.
- Stop/pivot triggers: unknown/mismatched board; M5 library/toolchain failure;
  portal loss during scan; any cache-off reset; UI-to-bridge coupling; second
  profile requiring code copies.
- Needs human verification: physical Tough display/touch/power behavior,
  provisioning usability, sustained playback control, and approval of the
  architecture/ADR before execution.

## Dissent (2026-08-04 solution boundary)

### Dissent Report

**Decision under review:** Build the Tough port as the first consumer of a
reusable M5Unified/M5GFX-backed family layer.

**Stakes:** This creates a long-lived C/C++ and hardware-family boundary and can
either reduce or multiply the cost of every later M5 device.

**Confidence before dissent:** MEDIUM

### Steel-Man Position

M5Stack already maintains board detection, PMIC, display, touch, button, audio,
and power support across its products. Wrapping that once behind HiPhi's semantic
contracts is more maintainable than transcribing vendor behavior per board, and
it lets Tough validate a path later M5 targets can reuse.

### Contrary Evidence

1. M5Unified's breadth can create a false promise that one binary safely supports
   every M5 model; its own examples branch on target SoC and board type.
2. The library is C++, while the controller core and current platform contracts
   are C; a leaky facade would spread C++ and vendor types through shared code.
3. A family abstraction can be premature after only one physical model; renamed
   Tough conditionals are not real reuse.
4. Retaining LVGL over M5GFX would leave two layers believing they own display,
   touch, buffers, and timing, increasing RAM and debugging cost.
5. The boot loop proves platform safety includes task-stack memory capabilities,
   not just drivers. M5Unified alone cannot solve that architectural defect.

### Pre-Mortem Scenarios

1. **Functional failure:** Tough renders but resets during NVS commits because
   another PSRAM-backed callback/task remains reachable from flash operations.
2. **Adoption failure:** Setup technically works but still asks users to type an
   SSID or loses the portal during scans, so first-run provisioning remains poor.
3. **Opportunity cost:** A generic M5 layer accumulates board switches before a
   second target exists and becomes harder to reason about than explicit profiles.

### Hidden Assumptions

| Assumption | Evidence | Risk if Wrong | Test |
|---|---|---|---|
| Vendor libraries work in native ESP-IDF 5.5.5 | Official manifests/CMake declare ESP-IDF and IDF 5 component dependencies | Port stalls or adds Arduino | Minimal pinned probe before migration |
| M5 board detection is reliable enough | Official example exposes `M5.getBoard()` and Tough enum | Wrong hardware profile starts | Exact K034 hardware assertion plus fail-closed allowlist |
| Direct M5GFX is enough | Required Tough UI is small and semantic | A second UI stack is reintroduced later | Implement one vertical screen and compare behavior/memory |
| One model can shape a reusable boundary | M5 APIs cover many devices | Over-generalized facade | Keep facade capability-based and prove with a host-only second profile |
| APSTA provisioning is stable | ESP-IDF supports scan in STA/APSTA, unlike AP-only | Portal disappears or radio work causes resets | Repeated hardware scan/connect/failure/retry soak |

### Reconstructed Story

- Still true: vendor libraries should own M5 hardware; shared product semantics
  must stay above them; Tough is the first hardware proof, not a special app.
- Weakest assumption: one physical M5 model is enough to define a family boundary.
- Changed situation model: source reuse is appropriate, but binary universality
  is not; profiles and artifacts must remain explicit and fail closed.
- Changed beliefs: confidence increased in the vendor-library choice and decreased
  in a single runtime-generic M5 target or LVGL bridge.
- Next action: add the ADR and risk-retirement work directly to PR #224, proving
  the pinned dependency, C ABI, explicit Tough allowlist, direct M5GFX vertical
  UI, first-run APSTA scan/select, and internal-stack persistence before
  migrating the rest of the Tough app in that same PR.

### Decision

**Recommendation:** ADJUST

Proceed with Option C only with SoC-specific artifacts, explicit model allowlists,
a narrow C ABI, direct M5GFX ownership, and the risk-retirement spike as the first
execution slice. Do not advertise generic M5 support from vendor detection alone.

**Confidence after dissent:** MEDIUM

**Follow-up artifact:** ADR required for the M5 family boundary and
M5Unified/M5GFX ownership decision, committed within PR #224.

## Salvage (2026-08-04 execution restart)

### Salvage Report

**Reason:** The first implementation accumulated local fixes and then failed on
the physical device; protecting that code would preserve the wrong architecture.

**Original Aim:** A verified Tough owner can provision Wi-Fi and control routine
listening from the device, with one PR also establishing reusable M5 seams.

### Learnings

1. The merged CI image must be flashed at device offset 0x0, not 0x1000; the
   former produced a blank screen while the latter boots the merged layout.
2. The exact artifact reached Wi-Fi, mDNS bridge discovery, zone parsing, and
   configuration persistence before resetting. This is real hardware evidence,
   not a compile-only failure.
3. The reset is an ESP-IDF cache-off stack-safety assertion: a task that can
   reach NVS/SPI-flash work cannot rely on plain xTaskCreate when PSRAM-backed
   allocation is enabled.
4. AP-only provisioning cannot satisfy first-run scan/select. ESP-IDF scan
   support requires STA/APSTA semantics, and the UI must expose scan results.
5. The spike does not use M5Unified or M5GFX as dependencies; hand-porting their
   board behavior is the wrong ownership boundary.

### Frame Shifts

- Tough-only platform adapter -> reusable M5 family component with explicit
  model profiles.
- Compile/build success -> exact-artifact hardware behavior and memory safety.
- Static SSID/AP setup -> first-run APSTA scan/select shared provisioning state.
- LVGL-owned M5 display path -> M5GFX-owned rendering behind a C facade.

### New Guardrails

- One delivery issue (#165) and one implementation PR (#224); no child issue/PR
  decomposition for this port.
- Vendor M5 libraries own M5 hardware behavior.
- Unknown or unqualified M5 models fail closed before product services start.
- Flash/NVS-capable tasks use explicitly internal-RAM stacks.
- First-run scan/select and portal availability during scans are acceptance gates.
- Green compile, static SSID entry, or a UI-only stack fix cannot retire these
  risks.

### Missing Context

The physical reset log should have been treated as an architectural boundary
failure before any UI polish or more target-local patches were attempted.

### Ownership / Coordination Breakdowns

The prior plan treated related epics as implementation dependencies and allowed
the PR body to call a hand-written spike complete. The maintainer's decision is
now explicit: #165 owns the outcome and #224 owns all implementation/evidence.

### Reusable Fragments

- Exact artifact SHA/flash-offset procedure and serial evidence.
- Internal-stack task primitive from the current uncommitted patch (retain only
  after auditing every flash-capable path).
- Existing shared semantic controller/config/provisioning contracts.

### Fresh Start Recommendation

Restart execution in this worktree with a pinned M5Unified/M5GFX probe, explicit
Tough allowlist, direct M5GFX vertical UI, shared first-run scan/select, and an
adversarial persistence stress test before migrating the remaining spike.

## Review (2026-08-04 solution boundary)

**Aim:** Deliver one physically verified Tough port through #165/#224 that
also establishes reusable M5 family seams and begins first-time setup with a
Wi-Fi scan/select flow.

**Status:** Adjust

### Alignment Check

- Necessary: Yes. Hardware evidence invalidated the hand-written driver,
  AP-only provisioning, and compile-only acceptance story.
- Aligned: Yes. The revised issue, PR, and session scope all require vendor
  M5 ownership, shared product semantics, first-run scan/select, and one PR.
- Sufficient: Yes at solution altitude. It avoids a universal UI rewrite while
  making second-M5 reuse testable.
- Mechanism clear: Yes. M5Unified/M5GFX own hardware; the C boundary and model
  profiles isolate that ownership; shared provisioning owns scan/recovery;
  internal stacks protect flash-capable paths.
- Changes complete: No. This review covers the corrected plan and source-of-
  truth, not an implemented port.
- Risks retired: No. The toolchain probe, exact-board assertion, direct-M5GFX
  path, APSTA soak, internal-stack audit, second-profile fixture, memory proof,
  and final exact-artifact test remain mandatory execution work in PR #224.

### Frame Check

The corrected frame explains the evidence: the spike is a hardware-learning
artifact, not the implementation base. The previous "one Tough adapter with
shared core untouched" frame collapsed when the device exposed copied vendor
behavior, target-local provisioning limitations, and a shared task-memory
contract defect. The reusable M5-family frame remains plausible and bounded.

### Drift Detected

- Solution drift: the original Tough-only LVGL adapter became a reusable
  M5Unified/M5GFX family port. Impact: #224 must be substantially revised in
  place. Route: Adjust within the same issue and PR.
- Scope correction: first-run Wi-Fi scan/select is now explicit blocking scope,
  not a later enhancement. Impact: APSTA scanning and shared provisioning
  state require hardware evidence. Route: Adjust within #224.
- Delivery clarification: all work remains in #165/#224. Related issues/epics
  are context only. Route: Continue with one PR.

### Risk Retirement Review

Every risk named in the superseding Solution Space remains open but has an
adversarial check and stop/pivot trigger. None may be treated as retired by the
existing green compile or by the four-file boot-loop patch.

### Needs Human Verification

- Physical Tough display/touch/power behavior under M5Unified/M5GFX.
- First-run scan/select usability and portal availability during scan/failure.
- Sustained bridge control and persistence stress on the exact final CI artifact.
- Maintainer acceptance of the family-boundary ADR in PR #224.

### Decision

Adjust and continue in the same PR. The source-of-truth now matches the user's
delivery constraint and observed hardware evidence; execution must retire the
listed risks before the PR can be called complete or leave draft.

### Next Steps

Implement the ADR and risk-retirement vertical slice directly in #224, then
migrate the remaining Tough port, implement shared first-run Wi-Fi scan/select,
run automated checks, and perform exact-artifact hardware acceptance.

## Execute (2026-08-04 hardware slice)

### Implemented

- Pinned `m5stack/m5unified` 0.2.19 and its `m5stack/m5gfx` 0.2.26 dependency.
- Added reusable `components/m5_platform` C ABI with explicit M5 Tough board
  qualification and fail-closed unknown-board behavior.
- Replaced the Tough LVGL/panel/touch transcription with direct M5GFX rendering
  and M5Unified touch/PMIC/display initialization; retired the duplicate files.
- Added shared bounded Wi-Fi scan results, APSTA scan promotion, rescan route,
  portal SSID selection with hidden-network fallback, and scan status on-device.
- Deferred AP mode, APSTA promotion, and STA connect work off ESP's `sys_evt`
  callback after physical stack-overflow evidence.
- Moved the bridge polling worker to an internal-RAM stack because discovery,
  zone, and remote-preference paths can persist NVS configuration.

### Automated evidence

- `idf.py -C tough_app build`: pass; app image 0x12cxxx, 70% partition free.
- `idf.py -C tough_app merge-bin`: pass; merged image SHA-256
  `271954cbd70c102efe56c700abaf2afc6041e56c09a8fb0a6cbdea5a614e4546`.
- `sh tests/run_wifi_provisioning_lifecycle.sh`: pass.
- `python3 scripts/check_controller_dependencies.py`: pass.
- `scripts/ci_sanity.sh`: blocked by pre-existing missing `pc_sim/` directory.
- Shared playback parsing now accepts JSON whitespace around `is_playing`, and
  the shared action router supplies a zone ID when a provider omits a label.
- Shared bridge request construction now normalizes unavailable battery levels
  to a valid numeric value; the upstream UHC contract issue is tracked in
  `open-horizon-labs/unified-hifi-control#458`.

### Physical evidence

- Exact board: ESP32-D0WDQ6-V3 revision 3, 16 MB flash, 8 MB PSRAM (4 MB
  mapped); M5Unified qualified `M5Tough`, 320x240 display, touch enabled.
- Direct merged-image write at device offset `0x0` succeeded during initial
  bring-up. Subsequent validation used NVS-preserving writes at `0x1000`,
  `0x8000`, `0xd000`, and `0x10000`; `0x9000` was deliberately untouched.
- First-run setup: AP `hiphi-tough-setup`, portal/DNS at `192.168.4.1`, APSTA
  scan completed with 24 networks, portal client received `192.168.4.2`.
- User selected `217IoT`; device saved credentials, rebooted, connected at
  `192.168.1.131`, initialized mDNS, discovered `http://NAS2:8088`, and loaded
  11 zones without `sys_evt` resets after deferred connect.
- An NVS cache-off assertion was reproduced during bridge persistence and
  addressed by moving the bridge worker to an internal stack. The final image
  retained the saved Wi-Fi/bridge/zone configuration and reached OPERATIONAL
  without a reset during the observed soak.

## Execute (2026-08-04 album-art and display policy)

### Implemented

- Kept artwork URL construction, image-key ownership, playback state, and
  display-policy values in shared bridge/config code; Tough only owns the
  M5GFX renderer and M5Unified brightness/sleep primitives.
- Added live RGB565 artwork fetch on an internal-RAM worker with a bounded
  240x240 payload, UI-thread handoff, stale-result rejection, and explicit
  FreeRTOS task deletion. The normal view uses a muted full-screen artwork
  background with readability bands; art mode uses the full-strength artwork
  background. The no-art state is now centered and no longer overlaps volume.
- Added the second power-saving stage (dim) and the existing soft-sleep stage
  as a shared art -> dim -> sleep policy. Tough is treated as externally
  powered because it has no battery, so shared defaults keep sleep disabled
  while permitting art and dim. Deep sleep remains disabled pending a
  qualified Tough wake source.
- Added `/settings` and `/api/settings` to the Tough STA server. The form uses
  the same presence-aware `controller_config_merge_remote_preferences()` path
  that Unified Hi-Fi Control will use, covering battery/USB art, dim, and sleep
  enable/timeout values.
- Added C linkage guards to shared HTTP/bridge headers used by the C++ target
  renderer; this preserves one shared implementation instead of a Tough-local
  parser or URL contract.

### Automated evidence

- `idf.py -C tough_app build`: pass; app image `0x12e280`, 70% partition free.
- `sh tests/run_wifi_provisioning_lifecycle.sh`: pass.
- `python3 scripts/check_controller_dependencies.py`: pass.
- `git diff --check`: pass.
- Final merged artifact SHA-256:
  `94402b3f46ba31f36a9dd6b841ff742a931d65d0391ae35e40bfbc2bfe33ccce`.

### Physical evidence

- Flashed app only at `0x10000`; NVS partition at `0x9000` was not erased or
  written. Saved Wi-Fi, bridge, and zone configuration remained intact.
- The first artwork implementation exposed a real task-lifecycle fault and
  bootlooped immediately after the bridge image-key update. The log identified
  an `IllegalInstruction` return from `artwork_fetch_task`; the worker now uses
  an internal stack, requests uncompressed bounded RGB565, and calls
  `vTaskDelete()` explicitly.
- The corrected artifact reached 192.168.1.131, loaded 11 zones, fetched live
  artwork (`Artwork ready ... (240x240)`), and remained stable through the
  observed post-fetch run. `/settings` returned HTTP 200 and rendered all art,
  dim, sleep, and deep-sleep safety copy.

### Remaining hardware check

- Verify the visual hierarchy on the physical panel (muted background in the
  normal view, full background in art mode, and touch wake from sleep) and let
  the device cross the configured 60-second art transition while stationary.

## Review (2026-08-04 artwork adaptation)

**Aim:** Add album-art thumbnail/art mode and a second power-saving stage to
the Tough without moving playback/config semantics out of shared code.

**Status:** Adjusted and physically rechecked.

### Alignment Check

- Necessary: Yes. The user rejected the initial full-background control layout
  because it obscured the control affordances.
- Aligned: Yes. The adapted control view is thumbnail-first; art mode remains
  full-screen, and power settings still use shared config merge semantics.
- Sufficient: Yes for this slice. M5GFX buffering, byte order, settings task
  capacity, and art/dim transitions were all exercised on the Tough.
- Mechanism clear: Yes. `M5Canvas` renders one complete frame, then
  `pushSprite()` presents it; `setSwapBytes(true)` matches the bridge RGB565
  byte order; the shared policy selects art, dim, and sleep stages.
- Risks retired: The prior artwork task return fault and settings HTTP stack
  overflow were reproduced and corrected. Visual confirmation remains a human
  checkpoint.
- Changes complete: No. Final review/PR evidence still needs the eventual git
  commit SHA and maintainer visual acceptance.

### Drift Detected

- Presentation drift: the first proposed full-background normal view competed
  with transport/volume controls. Impact: control mode was adapted to a
  thumbnail and dedicated volume row. Route: continue; this is the requested
  native adaptation, not a shared-model change.
- Stability drift: the settings route exposed an HTTP task stack overflow when
  exercised. Impact: STA server stack increased to 16 KiB and the endpoint was
  retested with HTTP 200/302 responses. Route: continue with the evidence
  recorded.

### Needs Human Verification

- Confirm the corrected RGB565 colors and the absence of visible flicker on the
  physical panel.
- Confirm the thumbnail/volume/transport hierarchy is comfortable at a glance.
- Confirm touch wake and the 60-second art transition under the restored policy.

## Execute (2026-08-04 picker follow-up)

- Reworked picker touch tracking to use absolute press/move coordinates rather
  than relying only on per-frame delta values; dragged releases cannot select a
  row accidentally.
- Added a proportional scrollbar thumb and visible “swipe” affordance whenever
  the zone count exceeds the viewport, and corrected the shared scroll clamp to
  stop at the last fully visible row.
- Reflashed app-only at `0x10000`; final merged artifact SHA-256:
  `424eb3e2382c39a26848a906f05c18471fa90588a2a67a1fc46a5ea82c3f1e9d`.
- Hardware remained connected at `192.168.1.131`, fetched artwork, and served
  `/settings` and `/zones` with HTTP 200 after the picker build.

## Dissent (2026-08-04 artwork adaptation)

**Decision under review:** Use a PSRAM-backed `M5Canvas` for buffered Tough UI
frames and keep artwork transport on the internal worker.

### Contrary Evidence and pre-mortem

1. A full-screen 16-bit canvas could consume scarce internal RAM if M5GFX does
   not honor its PSRAM canvas path; build/runtime memory evidence must remain
   clean.
2. A future bridge image format or byte order could make `setSwapBytes(true)`
   wrong for another M5 target; the format contract must stay explicit.
3. A large HTML handler can still exhaust the server task if settings grow;
   the current 16 KiB budget is a bounded mitigation, not permission to build a
   second web application.

### Decision

**Recommendation:** ADJUST/PROCEED. The canvas is the right Tough-native
flicker boundary, the current device has 4 MiB mapped PSRAM and the canvas
allocates there, the bridge response is explicitly RGB565 and was fetched at
240x240, and the settings route now survives a real POST. Keep the web form
small and treat any future format as a negotiated shared contract.

## Ship (2026-08-04)

**Shipped:** Tough firmware CI artifact and release GitHub Pages flasher.
**Target:** `build-tough` on PR/tag workflows, tagged release assets, and
`flash-tough.html` at the Pages root.
**Path:** source → `build-tough` → `hiphi-tough-firmware` artifact → tagged
release → `deploy-pages` → browser upload page.

- Tough CI selects classic ESP32 and emits the correct `0x1000` bootloader,
  `0x8000` partition, `0xd000` OTA data, and `0x10000` application layout.
- `manifest-tough.json` is checked for NVS preservation with the target-specific
  bootloader offset.
- Delivery-path tax remains physical hardware validation: CI and Pages prove
  artifact provenance, but a tagged release still needs a Tough stability check.

## Rebase and RLCD integration (2026-08-04)

- Rebased the Tough work onto `origin/master` at `575ec312`, which already
  carries the Waveshare ESP32-S3-RLCD-4.2 target from #225.
- Kept the shared asynchronous Wi-Fi scan contract (`wifi_mgr_scan_start`,
  `wifi_mgr_scan_state`, `wifi_mgr_scan_results_copy`) as the only scan API;
  updated Tough's portal and on-device setup view to use it.
- Added C linkage guards to `common/wifi_manager.h` for the M5 C++ renderer.
- Excluded the Tough-only `m5_platform` component from the RLCD build so the
  target does not resolve M5Unified/M5GFX dependencies.
- Verified both target builds and `tests/run_wifi_provisioning_lifecycle.sh`
  under ESP-IDF 5.5.5.
