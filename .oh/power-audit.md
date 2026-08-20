# Cross-target power audit — issue #228

## Session

- Phase: execute, with hardware validation still open
- Updated: 2026-08-20
- Working branch: `codex/issue-191-atom-s3-joystick`
- Starting commit: `97b700f4f69ad0c55297c3e79e09ddcff85a7332`
- M5 beta reference: `codex/issue-226-m5-betas` at
  `3296917` (read-only during this slice)
- Issue: <https://github.com/muness/roon-knob/issues/228>
- User observation: all tested low-power modes have unacceptable battery life;
  no exact-artifact current or energy traces were available for this audit
- Hardware status: builds and source contracts can be checked locally; no power,
  wake, reconnect, or sustained-radio claim is accepted until physical testing

## Problem

Owners need a controller that can stay useful on a small battery without merely
turning its pixels black while the processor, radio, second processor, PMIC, and
peripherals continue consuming near-active power.

The binding constraint is that “sleep” is not one operation. It is a hierarchy
of panel state, task cadence, radio association, SoC sleep, PMIC rail state, and
board-level always-on loads. The exact wake sources and controllable power
domains differ by target and hardware revision.

Success has two levels:

1. Before measurement, remove source-proven low-hanging drains without changing
   active UI performance, stored settings, BLE bonds, or qualified wake paths.
2. With measurement, establish reproducible current/energy budgets for active,
   connected idle, device deep sleep, wake, and reconnect; then optimize the
   dominant remaining domain rather than guessing.

## Situated audit

### Cross-target findings

- Every current ESP target enables ESP-IDF power management and tickless idle,
  but most targets did not call `esp_pm_configure()`. Configuration alone did
  not enable automatic Light-sleep.
- Shared station startup explicitly selected `WIFI_PS_NONE`, overriding
  ESP-IDF's documented `WIFI_PS_MIN_MODEM` default on every target.
- Several target “sleep” paths only set brightness to zero and issue panel
  sleep. Their application, Wi-Fi, BLE, timers, HTTP service, and polling loops
  remain active.
- A compile proves only that the chosen APIs and components link. It does not
  prove that tasks block long enough for automatic Light-sleep, that an AP's
  DTIM policy is favorable, or that a board-level rail is off.

### Exact target inventory

| Target | Before this slice | Dominant source-proven gap | This slice | Still open |
| --- | --- | --- | --- | --- |
| Waveshare HiPhi Dial / ESP32-S3 Knob 1.8 | LCD/backlight soft sleep; S3 Deep-sleep after the configured timeout | Shared Wi-Fi forced fully awake; RTC peripheral forced on; BLE not quiesced; backlight output not held; second ESP32 cannot be shut down by S3 | Modem sleep baseline, automatic Light-sleep, qualified ext1 wake, BLE quiesce, digital backlight hold, auxiliary-ESP32 parking image | Board current, wake/reconnect, fixed rails, PMIC/charger losses |
| HiPhi Frame / Waveshare PhotoPainter | E-paper controller sleeps after refresh; S3 and services stay connected | “Panel deep sleep” was being treated as if it were device sleep | Shared modem sleep and opportunistic S3 Light-sleep | Issue #160 default-off S3 sleep manager; PMIC rail work remains bench-gated |
| Waveshare RLCD 4.2 | Connected, static reflective display | Shared Wi-Fi explicitly fully awake; no target power state machine | Shared modem sleep and opportunistic Light-sleep | Exact usage model, input wake, and whether connectionless operation is desirable |
| M5Stack Tough | Backlight/panel sleep via M5Unified; application continues at 100 Hz | No SoC/PMIC sleep; frequent input/UI wakeups | Shared modem sleep, opportunistic Light-sleep, asleep input cadence reduced to 20 Hz | Qualify touch/button/RTC wake before using official `Power_Class` deep/timer sleep |
| AtomS3 + Atom JoyStick | Panel sleep; connected controller loop remains active | No SoC sleep; base has its own battery and controller behavior | Shared modem sleep, opportunistic Light-sleep, asleep input cadence reduced to 20 Hz | Decide whether battery runtime or always-connected responsiveness owns the profile |
| M5Dial beta at `3296917` | No configured dim/sleep policy | Official board has a power-hold shutdown path, but beta does not invoke it | Audited only | Add exact M5Dial policy using official wake/power-off semantics |
| M5StickS3 beta at `3296917` | No configured dim/sleep policy | Panel/SoC remain active | Audited only | Qualify exact revision, button/IMU/RTC wake, then add target policy |
| M5StopWatch beta at `3296917` | After 8 s sets brightness to 20 and marks the UI sleeping | Screen remains lit; SoC remains active | Audited only | Turn the panel off and qualify official target wake/power behavior |
| M5Stack Chan beta at `3296917` | No idle sleep policy | Connected loop stays active; servo/motion power is a target-owned load | Audited only | Explicit idle pose, torque/rail shutdown, wake policy, and recovery test |

### Dial board-level evidence

The exact Waveshare schematic changes the interpretation of the observed drain:

- The product is a dual-MCU board: an ESP32-S3 plus a classic ESP32.
- The classic ESP32 `CHIP_PU`/enable net is pulled to 3.3 V through R54 (10 kΩ).
  The schematic exposes no shutdown/reset control from the S3, so S3
  Deep-sleep cannot park that processor.
- The S3 encoder inputs GPIO7/GPIO8 have external 10 kΩ pull-ups (R60/R59).
  The S3 therefore does not need the whole RTC peripheral domain forced on
  merely to preserve internal wake pull-ups.
- GPIO47 drives the LCD backlight FET and has a 10 kΩ gate pulldown. Explicitly
  holding the GPIO low makes the intended Deep-sleep state deterministic.
- The classic ESP32's GPIO32 drives the DAC XSMT input. The DAC rail, haptic
  controller, microphone, SD interface, and other fixed loads are not all
  controllable by the primary firmware.

The official vendor archive contains `ESP32-KNOB_ESP32_0.bin` (SHA-256
`0c1c21b9822d4c2d80d58534b33eb0083880de4ed7354a38b4c78ba51757349d`).
Its embedded program strings include active application paths for Classic
Bluetooth, A2DP/AVRCP, BLE/BT HID, UART, encoder, and audio services. That proves
the shipped image contains those stacks; it does **not** by itself prove a
particular steady current or continuous radio transmission.

### Frame boundary

`eink_display_sleep()` sends the panel controller's deep-sleep command after a
refresh. The Frame application still runs its 50 ms UI loop, 10 ms button
timer, network polling, settings server, Wi-Fi, BLE, and PMIC configuration.
Calling this “device deep sleep” would be false.

The first-party PhotoPainter power demo performs ESP32-S3 Deep-sleep and writes
AXP2101 registers 0x26, 0x80, 0x90, and 0x91. Issue #160 deliberately forbids
copying that rail sequence before exact-revision bench proof because the
available schematic/tutorial/demo evidence is inconsistent about the e-paper
rail and restore sequence. This audit preserves that safety boundary.

### M5 boundary

The local M5Unified implementation used by these builds defines
`M5.Display.sleep()` as brightness zero plus panel sleep. It does not enter SoC
Deep-sleep or power the device off. M5Unified also provides `Power_Class`
`lightSleep`, `deepSleep`, `timerSleep`, and `powerOff`, but the correct behavior
is target-specific:

- Tough has an AXP192 and BM8563 RTC; M5Stack documents Light-sleep,
  Deep-sleep, and timer-sleep examples.
- M5Dial has a power-hold/wake circuit. M5Stack reports about 1.9 µA for the
  original Dial on battery in its qualified power-off state (and different
  figures for Dial V1.1). That number is not evidence about the Waveshare HiPhi
  Dial and must never be transferred between products or revisions.
- M5Stack Chan additionally owns motors/servos. CPU sleep without torque/rail
  policy can leave a large board-level load untouched.

## Solution Space

### Problem

How should HiPhi controllers minimize energy while preserving the interaction
contract, recovery, and exact-hardware safety?

### Key constraint

A global “sleep” Boolean cannot safely own display, radio, BLE, wake GPIOs,
PMICs, second processors, and motors across unrelated boards. Shared code can
own connection policy and transient service quiesce; target profiles must own
power domains and wake qualification.

### Working story

Remove universally wasteful connected-idle settings first, then make each
target's state explicit: active → connected idle → true device sleep. Measure
the whole transition, including wake/reconnect energy, before reaching for
revision-sensitive rail writes.

### Success signal

- No shared STA target unconditionally forces `WIFI_PS_NONE`.
- Automatic Light-sleep is enabled without overwriting a target's DFS range.
- A target that claims true Deep-sleep has qualified wake, transient BLE
  quiesce, deterministic output state, and fail-awake behavior.
- FNB-C2 traces show repeatable reductions in average energy for the intended
  workload, not merely a lower instantaneous screenshot.
- Exact artifacts pass sustained boot, input, display, Wi-Fi, BLE, and recovery
  checks before public beta.

### Decision criteria

1. Expected whole-board energy impact.
2. Confidence in exact target/revision facts.
3. Interaction latency and offline behavior.
4. Recovery and bricking risk.
5. Ability to measure the result independently.
6. Reuse across targets without hiding target-owned hardware.
7. Reversibility and optionality for later optimization.

### Critical assumptions

- `WIFI_PS_MIN_MODEM` does not violate polling/reconnect behavior on the user's
  AP and RSSI conditions.
- Tasks block for useful intervals; otherwise automatic Light-sleep will be
  technically enabled but rarely entered.
- The owned Waveshare Dial matches the cited dual-MCU schematic.
- Parking the unused classic ESP32 removes a meaningful load; magnitude remains
  unmeasured.
- A 20 Hz asleep input loop still detects real Tough/Atom interactions.
- USB-input measurements predict battery runtime only after charger/PMIC and
  test-topology effects are separated.

### Candidate A — band-aid: darker pixels and longer polling

- Keep the current architecture and extend dim/sleep timers.
- Cost: low.
- Benefit: backlight savings on emissive panels.
- Failure mode: repeats the current mistake by leaving the radio, SoC,
  peripherals, and secondary processors active.
- Decision: retain dimming as one state, reject it as the power strategy.

### Candidate B — local optimum: shared connected-idle baseline

- Use modem sleep by default, enable opportunistic automatic Light-sleep while
  preserving target CPU-frequency policy, and reduce gratuitous asleep polling.
- Cost: low; applies to all current connected targets.
- Benefit: removes two source-proven cross-target drains without changing the
  network architecture.
- Failure mode: periodic tasks or BLE/Wi-Fi locks may leave little idle time;
  peripherals and PMIC rails remain untouched.
- Decision: selected as the first slice.

### Candidate C — reframe: explicit per-target energy state machines

- Shared state describes inhibitors and service quiesce; each exact target owns
  panel, wake sources, retained state, PMIC/rail actions, auxiliary processors,
  motors, and full-reboot reconciliation.
- Cost: medium, delivered one target at a time.
- Benefit: makes “true sleep” a testable board contract rather than a UI label.
- Failure mode: an incorrect inhibitor or wake source can make a controller
  unavailable; default-off rollout and fail-awake checks are required.
- Decision: selected as the durable direction. Dial hardening is included now;
  Frame follows issue #160; M5 profiles follow the exact beta targets.

### Candidate D — redesign: discontinuous controller

- Stop preserving a live connection. Wake periodically or on local input,
  fetch/reconcile state, perform work, update a retained display, then power
  nearly everything off.
- Cost: high; changes latency, server behavior, and possibly product intent.
- Benefit: best path to multi-week/month e-paper operation.
- Failure mode: poor immediate transport control, high reconnect energy, missed
  server changes, and more complex offline/recovery semantics.
- Decision: preserve as an option for Frame/RLCD if measurement shows connected
  idle cannot meet the runtime aim.

### Interpretive variety

- **Radio-first view:** the unconditional `WIFI_PS_NONE` is the primary shared
  error; fix it everywhere before changing hardware policy.
- **Board-first view:** the Waveshare Dial's second ESP32 and the Frame/M5 PMIC
  domains may dominate, so SoC firmware improvements alone may disappoint.
- **Interaction-first view:** immediate control is the product, so connected
  modem sleep is preferable to deep sleep unless wake/reconcile remains
  imperceptible.
- **E-paper-first view:** retained displays invite a discontinuous device; a
  permanently connected Frame may be the wrong product mode.
- **Measurement-first view:** there is no defensible winner until transition
  energy and duty cycle are measured, but source-proven zero-regret waste can be
  removed beforehand.

### Risk retirement

| Risk | Cheapest decisive evidence | Gate |
| --- | --- | --- |
| Modem sleep harms control reliability | Sustained polling/commands on weak RSSI and a representative AP; disconnect/recovery counts | Before public beta |
| Automatic Light-sleep rarely runs | Current waveform and optional ESP-IDF PM lock statistics | Before deeper task refactor |
| Dial cannot wake or wake-loops | Repeated encoder A/B wake, held encoder, cold boot, and reset tests | Before distributing main image |
| BLE teardown corrupts preference/bond state | Connected, disconnected, scanning, disabled, and teardown-timeout sleep cycles | Before public beta |
| Auxiliary image is flashed to the wrong SoC | Boot-log chip identity plus physical USB orientation checklist | Every auxiliary flash |
| Auxiliary ESP32 is not a dominant load | A/B board-energy trace with identical main firmware | Before further auxiliary work |
| 20 Hz M5 asleep input misses taps | Short/long/edge touch and button qualification | Before accepting cadence change |
| Frame PMIC write strands hardware | Register readback, rail voltages, completed e-ink refresh, repeated KEY/timer wake | Before any rail-gating code |
| Deep sleep costs more than it saves | Integrate sleep plus wake/reconnect energy across real idle durations | Before choosing timeout |

### Recommendation

Use B immediately and advance C target by target. Do not use D by default, but
revisit it for retained-display products if measured connected idle misses the
runtime aim. Never borrow a current number or wake recipe across product names.

### Execution handoff

This slice implements:

1. Shared `WIFI_PS_MIN_MODEM` at STA startup.
2. Shared automatic Light-sleep that reads and preserves any existing target
   min/max CPU range.
3. A 20 Hz asleep UI/input cadence for the current Tough/Atom path.
4. Dial ext1 wake preflight using the board's external pull-ups, with no forced
   RTC peripheral domain.
5. Dial transient BLE quiesce without changing the enabled preference or bonds.
6. Dial backlight GPIO47 low hold across S3 Deep-sleep and release at boot.
7. A separately flashed classic-ESP32 parking image that holds DAC XSMT low and
   enters Deep-sleep with no wake source.
8. CI packaging for the auxiliary image, deliberately excluded from releases.

It does not implement or claim:

- Frame S3 Deep-sleep or AXP2101 rail gating.
- M5 beta sleep/power-off behavior on the `3296917` branch.
- A measured current, percentage reduction, or battery-life number.
- A public beta or hardware-qualified artifact.

## Build and artifact evidence

All six local candidates build with ESP-IDF 5.5.5. The merged images below are
bench-test inputs, not hardware-qualified releases. The Atom image also contains
the user's pre-existing uncommitted `atom_app/main/touch_ui.cpp` change, so its
hash identifies this exact working tree rather than commit `97b700f` alone.

| Target | Merged image | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Waveshare Dial main ESP32-S3 | `idf_app/build/hiphi_dial_power_audit_merged.bin` | 2,157,264 | `bb225d6a812b77bd5c298ad262c1280a4953b70fe8c87626aa16901868266494` |
| Frame | `frame_app/build/hiphi_frame_power_audit_merged.bin` | 1,424,048 | `35f5c523af4a533397989f3e6289437dd47e837b940493f7e3f7006c63ee93bf` |
| RLCD 4.2 | `rlcd_app/build/hiphi_rlcd_42_power_audit_merged.bin` | 1,755,392 | `d19261a40c44f64576620fff0f0604aeb3a81ce4a4de05d63a73ea9973ff6862` |
| AtomS3 + JoyStick | `atom_app/build/hiphi_joy_power_audit_merged.bin` | 1,295,520 | `cda24dca4f3bc44fd477ecf8c800e85395060bbdf999ab7967672635996d0df4` |
| M5Stack Tough | `tough_app/build/hiphi_tough_power_audit_merged.bin` | 1,314,224 | `748b3d2270b7788673ee9f5dc97fde7e7a2095c484d3a9a043882d80773620ea` |
| Waveshare Dial auxiliary ESP32 | `knob_aux_app/build/hiphi_knob_aux_park_merged.bin` | 207,936 | `e15e4ade4320c1965fa3e43eeab8aafd6ae175a0602321fbd064110049caca29` |

Automated checks passed: Wi-Fi provisioning lifecycle, BLE-disabled stub,
controller dependency policy and negative fixture, Dial identity, workflow YAML
parse, and `git diff --check`.

## Fact-checker accuracy report

| Claim | Status | Evidence | Recommended edit / use |
| --- | --- | --- | --- |
| ESP-IDF station Wi-Fi power save defaults to `WIFI_PS_MIN_MODEM` | Verified | ESP-IDF 5.5 Wi-Fi API documentation | Say “documented default”; do not imply a fixed current because DTIM and traffic matter |
| Automatic Light-sleep requires PM configuration, tickless idle, and all tasks/locks permitting idle | Verified | ESP-IDF 5.5 Power Management guide | Say “enabled/opportunistic,” never “the device is now in Light-sleep” without a trace |
| Shared firmware forced `WIFI_PS_NONE` | Verified | Pre-change `common/wifi_manager.c` at `97b700f` | Safe to call a source-proven cross-target drain |
| Frame device Deep-sleeps after every e-paper update | Contradicted | `eink_display.c` sleeps only the panel controller; `main_frame.c` and services continue | Replace with “the e-paper controller sleeps; the device remains connected and active” |
| The exact Waveshare Dial is a dual-MCU ESP32-S3 + ESP32 board | Verified | Waveshare product wiki and schematic archive | Always name the exact Waveshare ESP32-S3-Knob-Touch-LCD-1.8 |
| The primary S3 can shut down the second ESP32 | Contradicted | Classic ESP32 enable is pulled to 3.3 V; no S3 control net appears in the exact schematic | Use separate auxiliary firmware; do not claim main-firmware-only theoretical minimum |
| The factory auxiliary image contains Bluetooth/audio/HID/UART application code | Verified for image contents | Official vendor binary SHA above and embedded program strings | Do not upgrade this to “continuous transmission” or a current number |
| Parking the auxiliary ESP32 will materially improve runtime | Unverified | Strong mechanism, no A/B energy trace | Present as the highest-value board hypothesis and measure it first |
| GPIO7/GPIO8 wake no longer requires RTC peripheral forced on | Verified for this schematic/API pairing | External 10 kΩ pull-ups in schematic; ESP-IDF ext1 hold behavior | Hardware-test both encoder directions and held-low recovery |
| GPIO47 can be held low across ESP32-S3 Deep-sleep | Verified | ESP-IDF GPIO hold documentation plus board backlight gate schematic | Release the hold before LEDC reclaims the pad on boot |
| `M5.Display.sleep()` is full device sleep | Contradicted | Local pinned M5Unified implementation sets brightness zero and panel sleep only | Call it “panel sleep” |
| M5Stack devices can reach microamp-class sleep | Partially verified, revision-specific | M5Stack reports 1.9 µA for original M5Dial battery power-off and 6 µA for Dial V1.1 battery-only sleep | Use only as target/revision potential, never as HiPhi Dial evidence |
| The FNB-C2 has a 20-bit ADC, 1 µA displayed current resolution, 0–6.5 A range, ±(0.5‰ + 2 digits) current accuracy, 2 sps–1 ksps low-speed waveform, and 9-hour logging | Verified | FNIRSI official FNB-C2 product/specification page | Resolution is not the same as guaranteed accuracy or low-current burden performance |
| An FNB-C2 alone measures internal battery-rail current while the device is self-powered | Contradicted by topology | Instrument is an inline USB-C VBUS tester; self-powered battery current does not pass through it | Use USB-path A/B tests; add a safe series battery fixture/bench instrument for true battery rail |

### High-risk unresolved claims

- No current or battery-life improvement is measured yet.
- The owned Dial board revision has not been visually matched to the cited
  schematic during this audit.
- Vendor binary contents do not establish the auxiliary ESP32's steady runtime
  state or contribution to total current.
- Auto Light-sleep residency and the identity of remaining PM locks are unknown.
- Frame's charging state is still Boolean; issue #160 requires a tri-state
  `{charging, not_charging, unknown}` before true sleep.
- M5 target wake behavior has not been physically qualified for these artifacts.

### Expert review and source gaps

- A hardware/power review is required before any Frame AXP2101 rail writes.
- The FNB-C2's behavior and burden at the bottom of its range should be checked
  against a known reference if microamp claims will be published.
- Exact AP beacon/DTIM settings, RSSI, battery voltage, temperature, and charger
  state must accompany any comparative measurement.
- A board photo/revision record should accompany the first Dial auxiliary flash.

### Corrections made during this audit

- Corrected “e-ink sleeps” to distinguish panel sleep from device sleep.
- Corrected the implied single-SoC Dial model to include the always-enabled
  auxiliary ESP32 and fixed peripherals.
- Corrected the assumption that GPIO7/GPIO8 require RTC pull-ups by checking the
  exact external pull-ups.
- Corrected the assumption that display wake needs `WIFI_PS_NONE`; modem sleep
  remains associated and outgoing traffic wakes the station.
- Corrected the shared PM implementation so enabling Light-sleep preserves a
  target's existing DFS range instead of overwriting it.
- Kept M5Dial current figures separate from the unrelated Waveshare Dial.

## FNB-C2 measurement protocol

### 1. Record the test identity

For every trace record:

- exact device and hardware revision;
- exact source commit plus whether the tree was dirty;
- SHA-256 of the merged firmware image(s), including the auxiliary image;
- power topology: USB through FNB-C2, battery connected/disconnected, or a
  separate battery-rail fixture;
- supply voltage, cable, charger, battery state, ambient temperature;
- AP model, beacon interval, DTIM, RSSI, and network load;
- BLE enabled/bonded/connected state, playback state, brightness, and timeouts.

### 2. Separate USB and battery questions

- For repeatable firmware A/B work, power the board through the FNB-C2 with the
  battery disconnected if the hardware permits that safely. This measures USB
  input energy and avoids charging current.
- If the battery must remain connected, first establish whether it is charging,
  full, or supplying load; otherwise the PMIC can reverse or mix the measured
  current.
- For true battery runtime, place an appropriate instrument/fixture in series
  with the battery. Do not infer microamp battery sleep directly from a
  USB-powered measurement.

### 3. Use paired baseline/candidate runs

Keep hardware and conditions fixed. Alternate baseline and candidate images,
rather than measuring all baselines one day and all candidates another day.
For the Waveshare Dial use a factorial sequence:

1. old main + vendor auxiliary;
2. new main + vendor auxiliary;
3. old main + parked auxiliary, if compatible and safe;
4. new main + parked auxiliary.

That separates shared/main-firmware gains from the second-ESP32 gain.

### 4. Measure states and transitions

After a consistent settle period, capture at least:

- boot peak and boot energy to ready;
- active connected idle, screen on;
- dim state;
- panel/soft sleep while connected;
- true SoC/board sleep where implemented;
- wake latency and energy through Wi-Fi/BLE/server reconciliation;
- stopped-zone polling idle and active playback/control traffic;
- BLE disabled, enabled/disconnected, and connected.

Use the FNB-C2 high-rate low-speed waveform mode for transitions and radio
bursts. For steady states, integrate energy for at least 10 minutes; use longer
runs when the polling/DTIM cycle is sparse. Repeat runs and report median plus
range, not a single display reading.

### 5. Decide using energy, not minimum current

For a candidate sleep timeout `T`, compare:

`idle_energy_without_sleep(T)`

against:

`sleep_entry_energy + sleep_energy(T) + wake_and_reconnect_energy`.

The break-even time determines whether true sleep helps a real listening gap.
A very low minimum current can still lose if the controller repeatedly wakes,
reconnects, refreshes, or charges.

## Review report

### Scope inspected

- Full working-tree diff excluding the user's pre-existing
  `atom_app/main/touch_ui.cpp`, `.impeccable/`, and Python cache changes.
- Shared Wi-Fi/PM lifecycle, Dial sleep entry, BLE host lifecycle, M5 slept-loop
  cadence, auxiliary ESP32 image, target build isolation, tests, and CI.
- Read-only target audit of `3296917` for M5Dial, StickS3, StopWatch, and
  StackChan power behavior.

### Findings

1. **Resolved during review:** the first shared PM draft set equal min/max
   frequencies and would have overwritten Dial's earlier 80–240 MHz DFS policy
   because Wi-Fi starts later. It now reads the current PM configuration and
   changes only `light_sleep_enable`.
2. **Hardware gate:** compile success cannot validate Dial wake, BLE teardown,
   auxiliary flashing orientation, M5 touch cadence, or current draw.
3. **Accepted policy change:** the old `wifi_power_save_enabled` setting no
   longer disables modem sleep during normal display wake. Treat MIN_MODEM as
   the supported STA baseline; retain `WIFI_PS_NONE` only as an explicit
   diagnostic override if one is later required.
4. **Usability risk:** 20 Hz asleep input sampling is expected to be adequate,
   but short touch/button behavior needs exact Tough/Atom tests.
5. **Delivery boundary:** auxiliary firmware is built and uploaded separately
   and is not included in a release, preventing accidental main-SoC flashing.

### Decision

**CONTINUE to draft hardware validation.** No source-level blocker remains for
the low-hanging slice. Do not call it fixed, publish current/battery claims, or
promote the firmware to beta until the hardware gates above pass.

## Dissent report

### Verdict

**ADJUST, then proceed.** The direction is sound, but “we enabled sleep” is not
yet equivalent to “battery life is solved.”

### Strongest contrary case

- Modem sleep may contribute less than expected if 10/50 ms tasks, BLE, HTTP,
  or a low-DTIM AP keep waking the system.
- The Dial's auxiliary ESP32, regulators, DAC path, and fixed peripherals can
  dominate after the S3 is asleep.
- The Frame may require a discontinuous product mode or PMIC work to improve by
  an order of magnitude; connected idle alone may not satisfy the aim.
- Deep sleep is not automatically better: reconnection and e-paper refresh
  energy can exceed the saved idle energy for short gaps.
- Changing Wi-Fi power policy everywhere creates a reliability regression
  surface that unit tests and builds cannot exercise.

### Adjustments accepted

- Make no numeric savings claim before paired traces.
- Preserve target CPU-frequency policy in shared PM setup.
- Treat the auxiliary ESP32 as a separate, recoverable, orientation-sensitive
  artifact with vendor recovery documented.
- Keep Frame PMIC work out of this slice and uphold issue #160's default-off,
  fail-awake contract.
- Measure transition energy and break-even time, not just steady minimum draw.
- Audit PR #227 targets now, but implement their exact power profiles only on
  the branch that contains them.

### Remaining blockers

- Exact-artifact physical power and recovery evidence.
- Frame tri-state charging/inhibitor model and qualified wake path.
- Exact M5 beta profile integration on `codex/issue-226-m5-betas`.

## Primary sources

- ESP-IDF 5.5 Wi-Fi API:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/network/esp_wifi.html>
- ESP-IDF 5.5 Power Management:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/power_management.html>
- ESP-IDF 5.5 Sleep Modes:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/sleep_modes.html>
- ESP-IDF 5.5 ESP32-S3 GPIO hold:
  <https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/peripherals/gpio.html>
- Waveshare exact Dial wiki:
  <https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8>
- Waveshare exact Dial schematic archive:
  <https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-schematic.zip>
- Waveshare exact Dial demo/factory image archive:
  <https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-Demo.zip>
- TI PCM5100A product/data sheet:
  <https://www.ti.com/product/PCM5100A>
- Waveshare PhotoPainter power demo:
  <https://github.com/waveshareteam/ESP32-S3-PhotoPainter/tree/main/04_PowerConsumptionTest>
- M5Stack Tough exact product documentation:
  <https://docs.m5stack.com/en/core/tough>
- M5Stack Tough wake examples:
  <https://docs.m5stack.com/en/arduino/m5tough/wakeup>
- M5Stack original Dial exact product documentation:
  <https://docs.m5stack.com/en/core/M5Dial>
- M5Stack Dial wake examples:
  <https://docs.m5stack.com/en/arduino/m5dial/wakeup>
- FNIRSI FNB-C2 official specifications:
  <https://www.fnirsi.com/products/fnb-c2>
