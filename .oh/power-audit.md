# Cross-target power audit — issue #228

## Session

- Phase: ship-to-draft, with exact-artifact hardware validation still open
- Updated: 2026-08-20
- Authoritative branch: `codex/issue-226-m5-betas`
- Starting commit: `32969178d73b2ec661ad51f264bc9bbb018dd733`
- Cross-target source baseline integrated from:
  `codex/issue-191-atom-s3-joystick` at
  `97b700f4f69ad0c55297c3e79e09ddcff85a7332`
- Firmware source: the commit containing this report; its exact SHA and build
  artifact hashes are recorded in issue #228 after the clean build completes
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
| Waveshare HiPhi Dial / ESP32-S3 Knob 1.8 | LCD/backlight soft sleep; S3 Deep-sleep after the configured timeout | Shared Wi-Fi forced fully awake; RTC peripheral forced on; BLE not quiesced; backlight output not held; second ESP32 cannot be shut down by S3 | Modem sleep baseline, automatic Light-sleep, qualified ext1 wake, BLE quiesce, digital backlight hold, permanent no-wake auxiliary-ESP32 Deep-sleep image | Board current, wake/reconnect, fixed rails, PMIC/charger losses |
| HiPhi Frame / Waveshare PhotoPainter | E-paper controller sleeps after refresh; S3 and services stay connected | “Panel deep sleep” was being treated as if it were device sleep | Shared modem sleep and opportunistic S3 Light-sleep | Issue #160 default-off S3 sleep manager; PMIC rail work remains bench-gated |
| Waveshare RLCD 4.2 | Connected, static reflective display | Shared Wi-Fi explicitly fully awake; no target power state machine | Shared modem sleep and opportunistic Light-sleep | Exact usage model, input wake, and whether connectionless operation is desirable |
| M5Stack Tough | Backlight/panel sleep via M5Unified; application continues at 100 Hz | No SoC/PMIC sleep; frequent input/UI wakeups | Shared modem sleep, opportunistic Light-sleep, asleep input cadence reduced to 20 Hz | Qualify touch/button/RTC wake before using official `Power_Class` deep/timer sleep |
| AtomS3 + Atom JoyStick | Panel sleep; connected controller loop remains active; required STM32F030 coprocessor continuously samples inputs | No S3 sleep; M5Stack's coprocessor firmware runs a 48 MHz busy loop with continuous ADC/DMA and exposes no sleep register | Shared modem sleep, opportunistic Light-sleep, asleep input cadence reduced to 20 Hz | The STM32 cannot be permanently off while retaining joystick input; qualify a separate interrupt-driven/duty-cycled coprocessor firmware or whole-board power-off |
| M5Dial beta | No configured dim/sleep policy | Panel, SoC, and radio remain active; exact board has a GPIO46 power-hold path | Shared dim → connected panel sleep → M5Unified power-off ladder; button/touch/encoder wake while connected; setup inhibitor | Original Dial cannot report USB/external power, so source classification and power-button recovery require hardware qualification |
| M5StickS3 beta | No configured dim/sleep policy | Panel/SoC remain active | Shared power ladder; button/IMU wake while connected; M5PM1 power-off; VBUS-aware external-power policy | Verify exact unit/revision, button wake after power-off, and raise-to-wake thresholds |
| M5StopWatch beta | After 8 s sets brightness to 20 and marks the UI sleeping | Screen remains lit; SoC remains active | Removed fake sleep; shared power ladder; haptic off before sleep; M5PM1 power-off; VBUS-aware external-power policy | Verify touch/button/raise wake, vibration shutdown, and PMIC power-button recovery |
| M5Stack Chan beta | No idle sleep policy | Connected loop stays active; servo rail and speaker remain enabled | Shared power ladder; speaker task/amp stopped; servo torque and VM rail disabled in connected sleep; AXP2101 power-off; peripherals restored on wake; VBUS-aware external-power policy | Qualify physical balance/load behavior, servo/speaker restoration, AXP wake, and the BSP motion task's residual 20 ms wake cadence |

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

The auxiliary image is now an explicit **always-off** product invariant. At the
first possible point in `app_main`, it mutes and holds DAC XSMT low, clears all
wake sources, forces the RTC peripheral/slow-memory/fast-memory domains off,
and enters Deep-sleep. It contains no log-drain delay, connected-idle state, or
task loop. Reset and bootloader entry are the only ways to execute it again,
and either path immediately returns to the same state. Because the board ties
the chip's enable high, this is the strongest software-off state available; it
is not literal rail removal or a zero-current claim.

### Multi-processor inventory

The supported exact-target inventory has two boards with separately
programmable general-purpose processors:

1. **Waveshare Dial:** ESP32-S3 main plus unused classic ESP32. The second chip
   contributes no HiPhi function, so permanent wake-less Deep-sleep is correct.
2. **AtomS3 JoyStick K137:** ESP32-S3 main plus an STM32F030F4P6 that owns both
   Hall joysticks, four switches, and battery ADC data. It cannot be turned off
   all the time without removing the controller's input surface. M5Stack's
   official v2 coprocessor firmware configures a 48 MHz system clock,
   continuous ADC/DMA, and an empty polling loop with no `WFI`; its published
   I2C map exposes values, address, versions, and bootloader entry but no sleep
   command. This is a real residual load and a separate firmware/hardware gate,
   not a reason to pretend the board is single-SoC.

StackChan's smart servos also contain control electronics, but connected sleep
already releases torque and removes their VM rail. The remaining targets use
dedicated display/touch/PMIC/peripheral controllers, not an unused application
processor that can safely receive an always-off image.

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

The pinned M5Unified 0.2.19 implementation used by these builds defines
`M5.Display.sleep()` as brightness zero plus panel sleep. It does not enter SoC
Deep-sleep or power the device off. The implemented beta ladder therefore keeps
that state explicitly named **connected display sleep**, then calls
`M5.Power.powerOff()` only after the separately configured deep-sleep timeout.
The pinned source resolves that API through exact board hardware:

- Tough has an AXP192 and BM8563 RTC; M5Stack documents Light-sleep,
  Deep-sleep, and timer-sleep examples.
- Original M5Dial has a GPIO46 power-hold/wake circuit. M5Stack reports 1.9 µA
  for its battery-only sleep condition. The same documentation says GPIO46
  power-off applies only without external USB. Its TP4057 status pins are not
  routed to the ESP32-S3, and M5Unified reports neither VBUS nor charge state,
  so firmware cannot reliably distinguish battery from external power on this
  exact revision. That limitation is recorded rather than guessed around.
- StickS3 and StopWatch use M5PM1. Pinned M5Unified dispatches `powerOff()` to
  M5PM1, and provides a VBUS voltage reading independent of active charging.
- StackChan's CoreS3 uses AXP2101. Pinned M5Unified dispatches `powerOff()` to
  AXP2101. The official StackChan BSP exposes servo VM rail control; M5Unified's
  speaker `end()` path also disables the amplifier callback.
- StackChan's official BSP starts a 50 Hz motion task even when idle. This can
  shorten automatic Light-sleep residency. Safely suspending it requires a BSP
  lifecycle API; force-suspending it by task name can freeze its private mutex,
  so that tempting patch is rejected pending measurement or upstream support.

For boards with a readable PMIC, external-power classification now treats VBUS
at or above 4 V as external power even when a full battery is no longer
actively charging. This fixes the platform contract's prior active-charge-only
interpretation. It cannot fix the original Dial's missing source signal.

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
- M5 connected-sleep input polling still sees brief real button/touch/encoder
  events, and the exact PMIC/power-hold path returns through the documented
  physical control.
- Cutting StackChan's servo VM rail while idle is mechanically safe for the
  user's installation and restores cleanly before the next gesture.
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
- Decision: selected as the durable direction. Waveshare Dial hardening and the
  four exact M5 beta profiles are included now; Frame still follows issue #160.

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
| 20 Hz M5 asleep input misses taps | Short/long/edge touch, encoder, and button qualification on every affected profile | Before accepting cadence change |
| M5 board power-off cannot recover | Repeated battery-only power-off and documented button/RTC wake on the exact revision | Before distributing M5 beta images |
| Original M5Dial misclassifies external power | USB, rear-terminal, and battery-only traces plus observed policy log; no numeric or safety claim from the Boolean alone | Before enabling unattended deployment |
| StackChan strands or jolts its body | Sleep during motion, torque release, VM rail measurement, manual displacement, wake, neutral gesture, speaker replay | Before distributing StackChan beta |
| StackChan BSP motion task dominates idle | Compare PM residency/current with body enabled/disabled; request a coordinated BSP suspend API if material | After first FNB-C2 ranking trace |
| Frame PMIC write strands hardware | Register readback, rail voltages, completed e-ink refresh, repeated KEY/timer wake | Before any rail-gating code |
| Deep sleep costs more than it saves | Integrate sleep plus wake/reconnect energy across real idle durations | Before choosing timeout |

### Recommendation

Use B immediately and advance C target by target. Do not use D by default, but
revisit it for retained-display products if measured connected idle misses the
runtime aim. Never borrow a current number or wake recipe across product names.

### Execution handoff

The authoritative branch implements:

1. Shared `WIFI_PS_MIN_MODEM` at STA startup.
2. Startup dynamic frequency scaling from crystal frequency to the configured
   maximum on all six app families, plus shared automatic Light-sleep that
   reads and preserves any target-owned min/max range.
3. A 20 Hz asleep UI/input cadence for the current Tough/Atom path.
4. Dial ext1 wake preflight using the board's external pull-ups, with no forced
   RTC peripheral domain.
5. Dial transient BLE quiesce without changing the enabled preference or bonds.
6. Dial backlight GPIO47 low hold across S3 Deep-sleep and release at boot.
7. A separately flashed classic-ESP32 always-off image that holds DAC XSMT low,
   disables every wake source and RTC retention domain, and enters Deep-sleep
   immediately without logging or a task loop.
8. CI packaging for the auxiliary image, deliberately excluded from releases.
9. One tested M5 beta transition policy across Dial, StickS3, StopWatch, and
   StackChan: dim → connected display sleep → exact-board power-off.
10. Physical M5 input wakes connected sleep; setup mode inhibits it; a late
    retained-artwork transition receives a fresh sleep window rather than
    permanently suppressing sleep.
11. M5 PMIC boards use VBUS presence rather than active charging alone when
    selecting external-power policy.
12. StackChan connected sleep stops speaker playback/I2S/amplifier, releases
    torque, cuts the servo VM rail, clears queued choreography, and restores
    enabled peripherals on wake.

It does not implement or claim:

- Frame S3 Deep-sleep or AXP2101 rail gating.
- A measured current, percentage reduction, or battery-life number.
- A safe original-M5Dial external-power detector; the hardware exposes none to
  the controller in the checked schematic/API.
- A public release or hardware-qualified artifact.

## Build and artifact evidence

The clean ESP-IDF 5.5.5 matrix for the report's containing commit covers nine
primary images plus the separately flashed Waveshare auxiliary image:

- Waveshare Dial main, Frame, RLCD 4.2, AtomS3 + JoyStick, and M5Stack Tough;
- M5Dial, M5StickS3, M5StopWatch, and StackChan betas;
- classic-ESP32 auxiliary parking firmware.

For each build, CI and local verification require PERF optimization, the exact
target flash size/profile, and `CONFIG_PM_DFS_INIT_AUTO=y`. M5 beta profiles use
independent build directories and target-specific generated sdkconfig files.
Merged-image byte sizes and SHA-256 values are intentionally recorded with the
exact containing commit in issue #228 after the final clean build, avoiding a
self-referential report commit or hashes from the superseded joystick worktree.

The complete host contract suite passes, including the pure M5 power ladder,
StackChan choreography, configuration ownership, input/action/presentation,
Wi-Fi provisioning lifecycle, BLE-disabled stub, controller dependency policy
and negative fixture, Dial identity, M5 hardware boundary, workflow YAML parse,
and `git diff --check`. macOS cannot run LeakSanitizer's leak detector; those
sub-runs self-skip as designed while their non-sanitized twins pass. GitHub CI
remains the Linux sanitizer authority.

## Fact-checker accuracy report

| Claim | Status | Evidence | Recommended edit / use |
| --- | --- | --- | --- |
| ESP-IDF station Wi-Fi power save defaults to `WIFI_PS_MIN_MODEM` | Verified | ESP-IDF 5.5 Wi-Fi API documentation | Say “documented default”; do not imply a fixed current because DTIM and traffic matter |
| Automatic Light-sleep requires PM configuration, tickless idle, and all tasks/locks permitting idle | Verified | ESP-IDF 5.5 Power Management guide | Say “enabled/opportunistic,” never “the device is now in Light-sleep” without a trace |
| Shared firmware forced `WIFI_PS_NONE` | Verified | Pre-change `common/wifi_manager.c` at `97b700f` | Safe to call a source-proven cross-target drain |
| Frame device Deep-sleeps after every e-paper update | Contradicted | `eink_display.c` sleeps only the panel controller; `main_frame.c` and services continue | Replace with “the e-paper controller sleeps; the device remains connected and active” |
| The exact Waveshare Dial is a dual-MCU ESP32-S3 + ESP32 board | Verified | Waveshare product wiki and schematic archive | Always name the exact Waveshare ESP32-S3-Knob-Touch-LCD-1.8 |
| The primary S3 can shut down the second ESP32 | Contradicted | Classic ESP32 enable is pulled to 3.3 V; no S3 control net appears in the exact schematic | Use separate auxiliary firmware; do not claim main-firmware-only theoretical minimum |
| The auxiliary ESP32 can be literally power-gated in firmware | Contradicted | Its enable is hard-pulled high and no controllable load switch is present | Define “always off” as immediate no-wake Deep-sleep with all RTC retention off; measure residual board current |
| The factory auxiliary image contains Bluetooth/audio/HID/UART application code | Verified for image contents | Official vendor binary SHA above and embedded program strings | Do not upgrade this to “continuous transmission” or a current number |
| Parking the auxiliary ESP32 will materially improve runtime | Unverified | Strong mechanism, no A/B energy trace | Present as the highest-value board hypothesis and measure it first |
| AtomS3 JoyStick is a single-processor target | Contradicted | M5Stack's exact K137 documentation and schematic identify an STM32F030F4P6 at I2C 0x59 | Include the coprocessor in current budgets and power-state design |
| Atom JoyStick's STM32 can be permanently disabled without losing controls | Contradicted | The exact schematic assigns both joysticks, four switches, and battery ADCs to the STM32 | Retain it while controls must wake; investigate a low-power coprocessor image or whole-board power-off |
| The factory Atom JoyStick STM32 firmware idles efficiently | Contradicted for checked source | Official v2 source selects 48 MHz, continuous ADC/DMA, and a busy main loop; the public register map has no sleep command | Treat it as a likely low-hanging residual load, but do not flash modified coprocessor firmware without recovery and exact hardware tests |
| GPIO7/GPIO8 wake no longer requires RTC peripheral forced on | Verified for this schematic/API pairing | External 10 kΩ pull-ups in schematic; ESP-IDF ext1 hold behavior | Hardware-test both encoder directions and held-low recovery |
| GPIO47 can be held low across ESP32-S3 Deep-sleep | Verified | ESP-IDF GPIO hold documentation plus board backlight gate schematic | Release the hold before LEDC reclaims the pad on boot |
| `M5.Display.sleep()` is full device sleep | Contradicted | Pinned M5Unified 0.2.19 sets brightness zero and panel sleep only | Call it “connected display sleep” in the implemented ladder |
| Pinned M5Unified `powerOff()` selects exact M5 hardware paths | Verified for source/API mapping | M5Unified 0.2.19 dispatches original Dial through `power_hold`, StickS3/StopWatch through M5PM1, and StackChan/CoreS3 through AXP2101 before ESP Deep-sleep fallback | Source qualification is not physical wake qualification; repeat on every exact unit |
| A full USB-powered M5 PMIC board is always “charging” | Contradicted | M5Unified separates `isCharging()` from `getVBUSVoltage()`; charge status can go false when full while VBUS remains readable | Treat valid VBUS as external power for M5PM1/AXP2101 targets |
| Original M5Dial firmware can reliably detect external USB/DC power | Contradicted | Exact schematic leaves TP4057 `CHRG`/`STDBY` unconnected to the S3 and M5Unified exposes no Dial VBUS channel | Record source as ambiguous; validate unattended behavior before deployment |
| StackChan panel sleep also turns off its servo/speaker domains | Contradicted before this slice | Official BSP exposes a separate servo VM switch; M5Unified speaker has a separate runtime/amplifier lifecycle | Explicitly end speaker and cut servo VM in the target platform path |
| M5Stack devices can reach microamp-class sleep | Partially verified, revision-specific | M5Stack reports 1.9 µA for original M5Dial battery sleep and 6 µA for Dial V1.1 battery-only sleep | Use only as target/revision potential, never as Waveshare HiPhi Dial evidence or a firmware result |
| The FNB-C2 has a 20-bit ADC, 1 µA displayed current resolution, 0–6.5 A range, ±(0.5‰ + 2 digits) current accuracy, 2 sps–1 ksps low-speed waveform, and 9-hour logging | Verified | FNIRSI official FNB-C2 product/specification page | Resolution is not the same as guaranteed accuracy or low-current burden performance |
| An FNB-C2 alone measures internal battery-rail current while the device is self-powered | Contradicted by topology | Instrument is an inline USB-C VBUS tester; self-powered battery current does not pass through it | Use USB-path A/B tests; add a safe series battery fixture/bench instrument for true battery rail |

### High-risk unresolved claims

- No current or battery-life improvement is measured yet.
- The owned Dial board revision has not been visually matched to the cited
  schematic during this audit.
- Vendor binary contents do not establish the auxiliary ESP32's steady runtime
  state or contribution to total current.
- Atom JoyStick STM32 current is unmeasured. Its official source is visibly
  active, but source alone cannot quantify its share of board energy.
- Auto Light-sleep residency and the identity of remaining PM locks are unknown.
- Frame's charging state is still Boolean; issue #160 requires a tri-state
  `{charging, not_charging, unknown}` before true sleep.
- M5 target connected-sleep and power-off wake behavior has not been physically
  qualified for these artifacts.
- Original M5Dial external-power status is unobservable through the checked
  controller pins/API; its default battery policy under external supply remains
  an explicit deployment risk, not a solved fact.
- StackChan's BSP motion task still wakes every 20 ms even with its servo rail
  off; whether that materially dominates connected sleep is unmeasured.

### Expert review and source gaps

- A hardware/power review is required before any Frame AXP2101 rail writes.
- The FNB-C2's behavior and burden at the bottom of its range should be checked
  against a known reference if microamp claims will be published.
- Exact AP beacon/DTIM settings, RSSI, battery voltage, temperature, and charger
  state must accompany any comparative measurement.
- A board photo/revision record should accompany the first Dial auxiliary flash.
- M5 qualification must record the exact runtime board ID, power input path,
  battery state, and physical wake control; product-family names are not enough.
- StackChan qualification must include mechanical safety with torque and VM off,
  not just an electrical current trace.

### Corrections made during this audit

- Corrected “e-ink sleeps” to distinguish panel sleep from device sleep.
- Corrected the implied single-SoC Dial model to include the always-enabled
  auxiliary ESP32 and fixed peripherals.
- Corrected the AtomS3 JoyStick model to include its required STM32F030
  coprocessor and the factory firmware's continuous 48 MHz/ADC workload.
- Corrected the assumption that GPIO7/GPIO8 require RTC pull-ups by checking the
  exact external pull-ups.
- Corrected the assumption that display wake needs `WIFI_PS_NONE`; modem sleep
  remains associated and outgoing traffic wakes the station.
- Corrected the shared PM implementation so enabling Light-sleep preserves a
  target's existing DFS range instead of overwriting it.
- Corrected the configuration-only PM gap by enabling ESP-IDF startup DFS on
  every current application profile.
- Corrected M5StopWatch's “sleeping” Boolean: its old path only set brightness
  to 20 and never slept the panel or SoC.
- Corrected M5 charging semantics on PMIC boards: external VBUS persists after
  active charging ends.
- Corrected StackChan's display-only model to include its speaker amplifier,
  servo torque, servo VM rail, and residual BSP task cadence.
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

## Review

**Aim:** remove all source-provable low-hanging power waste across the current
firmware families, with `codex/issue-226-m5-betas` as the authoritative branch,
while preserving exact-hardware recovery gates.

**Status:** CONTINUE to draft artifact; PAUSE before public beta/release.

### Alignment check

- **Necessary:** yes. Forced awake Wi-Fi, inactive startup DFS, display-only M5
  sleep, an unparked Dial auxiliary ESP32, and energized StackChan peripherals
  directly explain plausible whole-board waste.
- **Aligned:** yes. Each change either lowers connected-idle work, stages true
  board shutdown, or makes a hidden always-on load explicit.
- **Sufficient:** sufficient for the pre-instrument source pass, not sufficient
  for a battery-life claim. Frame PMIC work and StackChan BSP-task work remain
  gated because source evidence cannot prove their safe physical behavior.
- **Mechanism clear:** yes. Radio modem sleep + startup DFS + automatic
  Light-sleep address shared SoC/radio idle; target ladders separately address
  panels, PMIC/power-hold, BLE, a second MCU, haptic, speaker, and servo rail.
- **Changes complete:** yes for source, host contracts, CI gates, and build
  matrix; no for exact-artifact flash/wake/current verification.
- **Risks retired:** source and build risks are retired. Physical wake,
  interaction capture, AP reliability, mechanical behavior, and energy impact
  are explicitly accepted only as hardware gates.

### Frame check

The frame still holds: the failure was never one bad timeout; “sleep” had been
used for unrelated panel, SoC, radio, and board states. Evidence strengthened
the per-target state-machine frame. It did not justify a universal PMIC recipe.

### Drift detected

- **Branch drift.** Started as implementation on the joystick branch with M5
  read-only; user clarified `3296917` is the latest branch and must contain all
  fixes. Route: adjusted by replaying the cross-target slice onto that lineage
  and implementing the four M5 profiles there.
- **Scope expansion, authorized.** M5 inspection exposed StopWatch fake sleep,
  full-battery USB misclassification, and StackChan energized peripherals.
  These are low-hanging power defects inside the requested “ALL fixes” scope.

### Findings resolved during review

1. Shared PM originally risked overwriting target frequency ranges; it now
   preserves the effective range and only enables automatic Light-sleep.
2. Merely compiling PM support left non-Dial targets at a fixed startup
   frequency; every application profile now enables startup DFS and CI asserts
   it.
3. M5 active-charge status could go false on full USB power; PMIC targets now
   classify readable VBUS as external power.
4. Reapplying a config that disables the current dim/sleep state could leave a
   dark UI; policy application now wakes and reconciles that state.
5. Force-suspending StackChan's private motion task was rejected because it can
   freeze the BSP mutex; its residual cadence is a measurement-ranked follow-up.

### Needs human verification

- Exact revision/board ID and input capture at the 20 Hz connected-sleep loop.
- Every power-off and recovery path, including held controls and external power.
- Dial auxiliary USB orientation and recovery with the vendor image.
- BLE state/bond survival across repeated Waveshare Dial sleep cycles.
- StackChan torque/VM-off mechanical safety plus servo/speaker restoration.
- Sustained Wi-Fi control/reconnect behavior on representative AP/RSSI/DTIM.
- FNB-C2 energy traces; no model review can establish current or battery life.

### Decision

Continue through a local branch commit and bench-test artifacts. The completion
gate is intentionally open: do not mark issue #228 fixed or promote a public
beta until the exact-artifact hardware checklist is recorded.

## Dissent

**Decision under review:** ship one broad low-hanging power slice across shared,
Waveshare, and M5 firmware before instrumented measurements.

**Stakes:** a wrong sleep transition can make a physical controller unavailable;
a weak transition can create false confidence while board-level loads dominate.

**Confidence before dissent:** medium-high for source waste, low for magnitude.

### Steel-man position

The selected changes remove known waste with high mechanism confidence, use
official exact-board APIs, remain recoverable, and expose a clean state ladder
for the coming instrument to rank what remains. Waiting for the meter would
waste its first sessions rediscovering defects already proven in source.

### Contrary evidence

1. Automatic Light-sleep can be enabled yet achieve little residency when UI,
   bridge, BLE, HTTP, or the StackChan 50 Hz motion task keeps waking the SoC.
2. Original M5Dial cannot report external power, so a Boolean configuration
   model necessarily chooses the battery profile in an ambiguous state.
3. Waveshare Dial regulators/fixed peripherals or Frame PMIC rails may dominate
   after the implemented SoC/radio fixes.
4. Power-off/reconnect energy may exceed saved idle energy for short gaps.
5. M5Unified source dispatch proves API selection, not the owned unit's wake,
   balance, peripheral restore, or charger behavior.

### Pre-mortem scenarios

1. **Functional failure:** one target powers off and its expected input cannot
   recover it, or StackChan wakes with dead audio/body control.
2. **Adoption failure:** users disable the new ladder because reconnect or
   interaction latency makes the dedicated controller feel unreliable.
3. **Opportunity cost:** shared tuning produces a modest win while the Frame
   PMIC, Dial auxiliary/fixed rails, or StackChan BSP cadence dominates; effort
   should have shifted to discontinuous operation or board power domains.

### Hidden assumptions

| Assumption | Evidence | Risk if wrong | Test |
| --- | --- | --- | --- |
| MIN_MODEM preserves control reliability | ESP-IDF contract; outgoing traffic wakes STA | misses/reconnect storms | long run at weak RSSI and real AP DTIM |
| Tasks leave useful Light-sleep windows | tickless idle and longer sleeping cadence | negligible savings | trace plus PM-lock/residency diagnostic build |
| M5 power buttons recover exact artifacts | M5 docs and pinned `Power_Class` mapping | unavailable device | repeated battery/USB power-off/wake matrix |
| StackChan can safely depower servos at rest | official VM/torque APIs | mechanical drop/jolt or bad restore | loaded sleep-during-motion test and rail trace |
| Original M5Dial battery policy is acceptable when source is unknown | battery use case and recoverable button wake | external unit disconnects unexpectedly | USB/rear-terminal/battery policy observation |
| Auxiliary ESP32 is material | always-enabled SoC with active factory image | flashing risk for little gain | factorial main/aux A/B energy trace |

### Reconstructed story and decision

- **Still true:** several large mistakes were source-provable and should be
  removed before measurement.
- **Weakest assumption:** that automatic connected-idle mechanisms gain enough
  residency on the real task/AP workload to matter.
- **Changed situation model:** M5 is not one easy family; PMIC-equipped targets
  have good source observability, while original Dial power source and
  StackChan's BSP task remain explicit exceptions.
- **Recommendation:** ADJUST, then proceed to draft artifacts. Keep every
  numeric/hardware claim open, preserve the original-Dial ambiguity in logs and
  documentation, and let the first meter run rank residual tasks/rails.
- **Confidence after dissent:** high that the branch removes genuine waste;
  medium-low that it reaches the desired runtime without the next measured pass.

## Primary sources

- ESP-IDF 5.5 Wi-Fi API:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/network/esp_wifi.html>
- ESP-IDF 5.5 Power Management:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/power_management.html>
- ESP-IDF 5.5 Sleep Modes:
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/sleep_modes.html>
- ESP-IDF 5.5 classic ESP32 Sleep Modes (auxiliary processor):
  <https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/sleep_modes.html>
- ESP-IDF 5.5 ESP32-S3 GPIO hold:
  <https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/peripherals/gpio.html>
- Waveshare exact Dial wiki:
  <https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8>
- Waveshare exact Dial schematic archive:
  <https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-schematic.zip>
- Waveshare exact Dial demo/factory image archive:
  <https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-Demo.zip>
- M5Stack Atom JoyStick K137 exact product documentation and schematic:
  <https://docs.m5stack.com/en/app/Atom%20JoyStick>
- M5Stack Atom JoyStick internal STM32 firmware:
  <https://github.com/m5stack/Atom-JoyStick-Internal-FW>
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
- M5Stack original Dial schematic:
  <https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/499/Sch_M5Dial.pdf>
- M5Stack StickS3 wake examples:
  <https://docs.m5stack.com/en/arduino/m5sticks3/wakeup>
- M5Stack StopWatch exact product documentation:
  <https://docs.m5stack.com/en/core/StopWatch>
- M5Stack Chan exact product documentation:
  <https://docs.m5stack.com/en/StackChan>
- M5Stack Chan servo documentation:
  <https://docs.m5stack.com/en/arduino/stackchan/servo>
- Pinned M5Unified power API implementation (local build uses 0.2.19):
  <https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.cpp>
- FNIRSI FNB-C2 official specifications:
  <https://www.fnirsi.com/products/fnb-c2>
