# Normalized input events and runtime bindings — issue #194

## Session

- Phase: execute
- Status: local execution complete; final review CONTINUE and dissent PROCEED
- Updated: 2026-07-31
- Baseline: `023aba89aceb8b090af0b54cf4c104721b47a646`
- Branch: `codex/issue-194-input-bindings`
- Stack: #216 → #217 → #218 → #219 → #220 → this slice
- Coordinates with: #115, #170, #193, and
  `open-horizon-labs/unified-hifi-control#333`
- Agent policy: Terra review/dissent; no Claude/Sonnet or Superego tooling

## Confirmed Problem

Users need onboard and attached controls to behave consistently on Dial, Frame,
AtomS3 Joystick, M5Stack Chan, and later verified ESP32-S3 target profiles
while remaining bindable to playback, navigation, selection, and Home Assistant
actions. Today the same `controller_input_action_t` mixes physical input,
semantic commands, picker navigation, and presentation queries:

- Dial GPIO encoder ticks are interpreted by a UI-aware legacy binding;
- Dial touch callbacks emit command-bound enum values;
- BLE Consumer Control usages are converted back into that same enum;
- Frame GPIO buttons bypass the controller and call Wi-Fi/restart effects;
- target I²C buses are privately owned, so an accessory driver cannot safely
  share them;
- build-time driver inclusion and user-visible action meaning have no separate
  models.

The binding constraint is that input normalization must not become another
backend or UI protocol. Physical drivers must not know Roon, HQPlayer, Home
Assistant, picker presentation, manifest JSON, or bridge transport. Conversely,
semantic UI elements and BLE HID usages must not be forced through a fake GPIO
device model.

Success means:

1. compiled target profiles select drivers and transport resources;
2. drivers emit bounded, pointer-free physical events and declare capabilities;
3. runtime bindings map physical controls in an explicit interaction context to
   backend-neutral actions;
4. semantic touch/BLE/adaptive UI actions enter the same action router without
   pretending to be physical events;
5. target-local persisted overrides and server-delivered bindings have explicit
   version/generation ownership;
6. current Dial and Frame behavior remains releasable while the M5 drivers and
   adaptive server contract arrive in subsequent slices.

Only hardware whose exact ESP32-S3 identity is verified by #193 is eligible for
this path. The official non-S3 M5Stack Tough is excluded unless #193 proves the
owned hardware is a distinct S3 product/revision.

## Situated Evidence

### Current firmware

- `controller_input_action_t` combines volume, playback, menu, and navigation.
- `controller_legacy_binding` queries picker state, translates encoder velocity,
  executes media commands, and performs picker effects.
- Dial polls its GPIO7/GPIO8 quadrature encoder every 3 ms, coalesces ticks on
  its existing queue, and suppresses the wake-causing tick.
- Dial touch is interpreted by LVGL callbacks and is already semantic.
- Shared BLE HID receives standard Consumer Control usages and is already
  semantic.
- Frame BOOT/GP4 long presses directly start provisioning/restart; PWR is unused.
- Dial I²C0 on GPIO11/GPIO12 is privately created by `i2c_bsp` for touch/haptics.
- Frame I²C0 on GPIO47/GPIO48 is privately created by the PMIC source.
- `rk_cfg_t` is a monolithic versioned blob. Adding target-specific binding
  records to it would couple unrelated device profiles and manufacture a
  migration problem.

### Official M5 hardware facts

- AtomS3 is ESP32-S3FN8 with 8 MB flash. Its rear I²C bus is GPIO38/GPIO39 and
  is shared with the onboard MPU6886 at 0x68.
  <https://docs.m5stack.com/en/core/AtomS3>
- Atom Joystick uses the AtomS3 GPIO38/GPIO39 bus and an STM32 controller at
  0x59. It exposes two sticks plus left/right buttons and a 300 mAh battery.
  <https://docs.m5stack.com/en/app/Atom%20JoyStick>
- Unit Encoder is I²C 0x40 and provides a 30-position rotary encoder, a press
  switch, and LEDs.
  <https://docs.m5stack.com/en/unit/encoder>
- Unit Joystick v1 is I²C 0x52 with 8-bit X/Y plus a button; Joystick2 is I²C
  0x63 with 16-bit X/Y plus a button and supports changing its address.
  <https://docs.m5stack.com/en/unit/joystick>
  <https://docs.m5stack.com/en/unit/Unit-JoyStick2>
- Unit ByteButton has eight buttons, is I²C 0x47, and offers two I²C expansion
  ports for cascading.
  <https://docs.m5stack.com/en/unit/Unit%20ByteButton>
- Unit Button is direct digital GPIO, not I²C.
  <https://docs.m5stack.com/en/unit/button>
- Chain Joystick uses UART at 115200 bps and participates in a discovered,
  cascaded Chain topology rather than an address-only I²C bus.
  <https://docs.m5stack.com/en/chain/Chain_Joystick>

These are profile/resource differences, not action-model differences.

## Solution Space

| Option | Level | Approach | Main trade-off |
|---|---|---|---|
| A. Extend the action enum | Band-aid | Add joystick/button/HA enum members and more target callbacks | Keeps physical origin, UI state, and command meaning entangled |
| B. Normalized events plus static maps | Local optimum | Drivers emit common events; compile-time tables map them to fixed actions | Clean drivers, but every binding change still requires firmware |
| C. Capability registry, runtime bindings, and action router | Reframe | Separate physical events, compiled drivers, contextual bindings, and semantic actions | More value types and explicit lifecycle/version rules |
| D. Server-owned adaptive action graph | Redesign | Server sends the complete control graph and firmware becomes an interpreter | Cannot own boot/recovery offline; freezes a premature protocol |
| E. Generic HID/descriptor interpreter | Redesign | Represent every control as a descriptor and route all input through it | Over-general for GPIO/I²C/UART firmware and obscures safety policy |

### A — extend the current enum

- Solves stated problem: no. It adds device names to the mixed abstraction.
- Implementation cost: low initially.
- Maintenance burden: high and combinatorial.
- Second-order effects: touch, BLE, GPIO, picker, and integrations acquire more
  special cases; external modules remain impossible to diagnose generically.
- Optionality: poor.
- Rejected.

### B — normalized events with static maps

- Solves stated problem: partially. Driver reuse and deterministic behavior
  improve, but users still cannot bind controls without reflashing.
- Implementation cost: medium.
- Maintenance burden: medium.
- Second-order effects: target profiles risk becoming action profiles, leading
  to a binary for every desired mapping.
- Optionality: good stepping stone if its value types are not compile-time-only.
- Not selected as the end state; its default tables are retained as safe
  fallback data within C.

### C — capability registry, runtime bindings, and action router

- Solves stated problem: yes.
- Implementation cost: medium to high, divisible into releasable slices.
- Maintenance burden: low when transport, gesture, binding, and action ownership
  remain distinct.
- Second-order effects: requires stable source identity, context precedence,
  atomic binding replacement, validation, and a target-local persistence
  contract.
- Optionality: high. It supports current controls, future M5 accessories,
  server-delivered adaptive UI, and Home Assistant without importing backend
  strings into drivers.
- Selected.

### D — server owns every action graph

- Solves stated problem: partially while online.
- Implementation cost: high across firmware and server.
- Maintenance burden: medium.
- Second-order effects: provisioning, restart, wake, offline defaults, and
  recovery become dependent on a remote payload. A malformed/stale manifest can
  make the controller unusable.
- Optionality: high but unsafe as the only authority.
- Rejected as the core. A validated, generation-tagged server binding source is
  one input to C.

### E — generic descriptor interpreter

- Solves stated problem: theoretically.
- Implementation cost: very high.
- Maintenance burden: high for an embedded product.
- Second-order effects: runtime heap/parsing pressure, difficult safety review,
  and abstractions that fit neither Chain topology nor semantic touch elements
  well.
- Optionality: broad but speculative.
- Rejected.

## Recommendation

Select **C: capability registry, runtime bindings, and a semantic action
router**, delivered as a sequence of independently releasable slices.

This is a reframe: the problem is not “how does firmware support each new
control?” It is “who owns the transitions from transport sample → physical
gesture → contextual meaning → effect?” Each transition gets one value boundary
and one owner.

### Layer 1 — input-profile overlay and resource registry

An input-profile overlay compiles only relevant drivers and declares each
instance:

- stable logical source ID and human label;
- driver kind and instance;
- transport resource (GPIOs, shared I²C bus plus address, or UART);
- controls/capabilities;
- fixed/default bindings;
- whether a control/action is owner-rebindable or safety-locked.

The input overlay does not own full board identity. #193 owns verified SoC,
flash, PSRAM, display/touch/power facts, partitions, artifact geometry, and OTA
compatibility. #194 consumes that verified target identity and adds only input
resources, source/control identities, capabilities, and defaults.

The profile does not define a separate binary for each action mapping. Driver
inclusion is compile-time; binding selection is runtime.

A common resource registry validates duplicate GPIO ownership, I²C bus/address
collisions, unavailable buses, UART conflicts, and incompatible electrical
requirements before drivers start. A shared target bus owner, not each device
driver, creates the ESP-IDF I²C/UART resource and lends device handles to
compiled drivers.

Logical source IDs are profile-owned and stable across reboot. They do not
encode a mutable I²C address. Multiple identical accessories receive distinct
profile instance IDs. A binding may be persisted only when the device protocol
and profile prove a durable identity. Until Chain exposes such an identity,
dynamically discovered Chain slots are session-scoped and non-persistable;
unplug/reorder never transfers an old binding to a replacement node. Topology
loss is surfaced as unavailable capability.

### Layer 2 — normalized physical event and capability values

Introduce a bounded, pointer-free physical event with:

- stable source ID;
- control ID;
- event kind: button gesture, rotation, axis, or direction gesture;
- gesture: press, release, tap, double-tap, hold, repeat, or none as applicable;
- signed normalized value/delta;
- flags for initial/repeat/neutral transitions;
- monotonic sequence/timestamp metadata only when required for stale-event
  rejection, never wall-clock time.

The value contains no GPIO, I²C address, Roon/HQPlayer/Home Assistant name,
manifest string, JSON, URL, or presentation pointer.

Drivers own transport sampling and hardware calibration. Shared recognizers own
debounce, hold, double-tap, repeat, dead-zone, hysteresis, and direction
threshold rules so equivalent buttons/axes behave consistently. High-rate raw
axis samples remain inside the driver/recognizer unless an explicitly validated
continuous binding requests them; normal navigation emits edge/repeat events.

Capability values describe the controls and supported event/gesture ranges.
They allow settings/adaptive UI to offer only meaningful bindings and give
conflict/unavailable diagnostics without knowing the driver implementation.

### Layer 3 — contextual runtime bindings

A binding key is:

`{source_id, control_id, event kind/gesture, interaction context}`

The binding value contains:

- a typed semantic action template;
- a bounded transform for the event value (sign, scale, dead-zone, acceleration
  curve, or fixed value);
- repeat/consume policy;
- source priority: safety default, target default, local override, or current
  server generation.

Contexts are bounded identifiers, not UI pointers:

- global;
- media;
- zone picker;
- settings/recovery;
- current adaptive screen generation.

The controller interaction-state owner, not a renderer query, owns context
transitions. It synchronously records media/picker/settings/adaptive transitions
when the action router performs the corresponding presentation effect. The
resolver receives a copied bounded context snapshot when resolution begins; it
does not call `controller_presentation_is_zone_picker_visible()`.

Binding authority and precedence are:

1. immutable safety-locked exact-context binding;
2. immutable safety-locked global binding;
3. permitted target-local owner override for the exact context;
4. current server binding for its exact adaptive context, but only on controls
   the input profile marks server-bindable;
5. target default for the exact context;
6. permitted target-local global override;
7. target global default;
8. no action.

Server bindings cannot be global and cannot reference privileged system
effects. Local/server records cannot shadow a locked binding. Duplicate exact
keys reject the complete candidate table.

An inactive bounded table is fully validated before one atomic publication.
Queued physical events resolve against the table and copied context active when
resolution begins. A server snapshot atomically pairs its action table and
bindings and carries `{connection_epoch, generation}`. A reconnect creates a
new epoch; generation is monotonic only inside that epoch. Unknown, stale,
expired, revoked, malformed, or capability-incompatible references are rejected
with a diagnostic. In an active adaptive context they may fall through only to
a profile-declared adaptive-safe fallback (normally no action or Back), never
implicitly to a broad/global media binding. Outside adaptive context, ordinary
target-default precedence applies.

The initial bounded maxima are 16 sources, 64 total controls, 128 binding
entries, and 64 adaptive actions; profiles/builds fail rather than truncate.
These limits remain subject to exact target map/heap evidence before Slice C.

Target defaults are compiled immutable fallback data. Owner changes persist in
a separate, target-local, versioned binding namespace. They do not enlarge
`rk_cfg_t`, do not copy between Dial/Frame/Atom targets, and do not require or
permit cross-device migration. A schema change may validate or discard only
that device's incompatible binding records and return to safe defaults. Records
include target input-profile identity/version, source/control IDs, schema
version, bounded payload length, and integrity metadata; incomplete,
power-interrupted, mismatched, or unproven-source records fail closed.

### Layer 4 — semantic action router

Physical bindings resolve to a pointer-free tagged semantic action:

- controller command (`controller_command_t`);
- navigation/selection effect;
- privileged system effect;
- validated adaptive action reference.

Existing `controller_command_t` remains the only stable playback/volume command
value. The bridge continues to own operational readiness, volume clamp,
optimistic display, exact protocol JSON, and transport errors.

Navigation actions explicitly distinguish open picker, move, select, back, and
settings. The current “PLAY_PAUSE means select if picker visible” behavior is
removed; semantic touch callbacks send the actual intended navigation or media
action.

Provisioning, restart, factory reset, and wake policy are privileged system
effects. Frame's BOOT/GP4 defaults remain fixed and safety-locked unless a later
product decision explicitly makes a control rebindable. KEY/GP4 wake ownership
must remain compatible with #160.

Adaptive/Home Assistant bindings carry a validated action-table reference plus
the active `{connection_epoch, generation}`. They never embed raw JSON or
backend strings in the physical event or binding core. The #170/UHC #333 parser
owns validating server payloads and populating the action table. The router
checks current capability and authorization again at dispatch. Revocation or
epoch replacement invalidates old references immediately; Home Assistant
effects are not retained as executable offline payloads.

### Two ingress paths, deliberately

Not every input is a physical device event:

1. GPIO/I²C/UART drivers emit physical events into the binding resolver.
2. Semantic UI elements and BLE Consumer Control usages emit semantic control
   intents directly into the action router.

BLE play/pause, next, previous, and volume usages are already semantic. Forcing
them through a fabricated button/source binding would lose HID meaning and add
configuration ambiguity. They still pass through the router's current-context
policy so existing behavior is preserved:

- in media context, volume and play/pause become their media commands;
- in picker context, volume moves one entry, play/pause activates the selected
  entry, and next/previous remain ignored;
- in settings/recovery context, preserve the current non-picker fallback:
  volume/play/pause/next/previous remain media commands until a later explicit
  UX decision and characterization changes that policy;
- all resulting effects still pass readiness, authorization, generation, and
  privileged-effect checks.

Touch elements dispatch their explicit element action: transport buttons are
media commands, a zone-label tap opens the picker, a long press opens settings,
and a zone-list tap explicitly selects. This removes the accidental
“PLAY_PAUSE means select” encoding without changing user behavior.

### Ingress and queue policy

All effects execute on the existing target UI/controller actor. Slice A adds no
task. Driver/timer callbacks do not perform effects, block, or allocate.

Event classes have explicit mailbox policy:

- locked safety gestures use an idempotent per-action pending latch so queue
  pressure cannot silently lose provisioning/restart;
- press/release/tap/hold edges are ordered within declared mailbox capacity;
  overflow is a surfaced input fault, never an unreported drop;
- rotation deltas are saturating and coalescible per source/window; Slice A
  preserves Dial's current sum, sign, velocity thresholds, and reverse-motion
  behavior;
- axes are rate-limited samples inside a driver/recognizer and normally emit
  threshold/hysteresis/repeat edges rather than flooding the UI queue.

Every mailbox exposes accepted/coalesced/dropped/overflow counters. Full queues
return failure. Future high-rate drivers must prove their class policy with
saturation tests before inclusion.

## Accepted Trade-offs

- More small value types and registries exist than in the current enum design.
- Runtime binding replacement and persistence require validation/version rules.
- Source IDs must be declared deliberately by each profile.
- A shared bus/resource owner must precede external I²C/Chain driver work.
- Full Home Assistant actions depend on the coordinated #170/UHC #333
  versioned server contract; the firmware boundary can be implemented first,
  but the user-visible integration cannot be claimed early.

## Rejected/Deferred Mechanics

- No combinatorial firmware binaries for bindings.
- No raw driver events in integration payloads.
- No backend action strings or raw params stored in physical events.
- No “joystick always means volume” hard-coding.
- No new FreeRTOS task merely to introduce the value seam; the existing target
  input queue and UI/event-loop serialization remain until measurements prove a
  scheduling change is needed.
- No cross-device NVS/config import, restore, copy, reconciliation, or migration.
- No generic dynamically allocated descriptor language.
- No arbitrary rebinding of provisioning/restart/factory-reset safety actions.
- No direct reuse requirement for v4 code; salvage its learned action semantics
  and protocol decisions where they still fit.

## Delivery Slices

### Slice A — proven current-device spine

1. Add the bounded physical event, copied interaction context, minimal default
   resolver, semantic control intent/action router, and byte budgets.
2. Add immutable target-local input descriptors for the existing Dial encoder
   and Frame BOOT/GP4/PWR controls. These establish stable source/control IDs
   and defaults without implementing the general capability/resource registry.
3. Replace `controller_input_action_t` and `controller_legacy_binding`.
4. Route Dial encoder through the default contextual binding while preserving
   coalescing, acceleration, display wake, and wake-tick suppression.
5. Route Dial touch and Frame/Dial BLE media usages through the semantic router
   while preserving their exact media/picker context behavior.
6. Route Frame long-press system behavior through explicit safety-locked system
   actions without changing its BOOT/GP4 defaults; PWR remains unused.
7. Keep one existing UI/event queue and no new runtime task.

This slice is behavior-preserving and does not add NVS, a general resource
registry, an adaptive action table, or an accessory driver. It establishes the
production seam needed by all later slices.

### Slice B — resource ownership and AtomS3 + Atom Joystick vertical proof

After #193 verifies the exact owned AtomS3 revision:

1. Introduce the input-profile/resource registry as the sole creator of claimed
   GPIO/I²C/UART resources; migrate any target bus it owns rather than wrapping
   a second initializer around it.
2. Add only the #192 AtomS3 + Atom Joystick profile/driver (0x59 on
   GPIO38/GPIO39, coexisting
   with MPU6886 at 0x68).
3. Define an 8 MB Atom partition/memory profile from evidence; do not inherit
   Dial/Frame's 16 MB/PSRAM assumptions.
4. Build accessory-on/off profiles to prove driver exclusion and resource
   conflict failures.

Atom support is claimed only after exact artifact boot/display/input, shared-bus
coexistence, input latency, calibration/dead-zone, battery, reconnect, and
long-duration evidence.

### Post-Atom follow-on issue candidate — individually evidenced M5 Units

After the Atom proof, create or reconcile separately tracked leaf issues for
Unit Encoder, Unit Joystick2, Unit Button, and ByteButton, one individually
reviewable driver/profile at a time. Each addition proves address configuration
and collision behavior, gesture semantics, capability diagnostics, and hardware
evidence. ByteButton cascading is not treated as safely multi-instance until
its protocol proves durable addressing/identity. These are not bundled into
Slice B or treated as prerequisites for completing the Atom vertical proof.

### Post-Atom follow-on issue candidate — Chain topology

Create or reconcile a separate leaf issue for the Chain UART owner, framing,
enumeration, topology-loss, and reorder policy. Chain sources remain
non-persistable until durable device identity is proven. Chain is not bundled
into Slice B.

Hardware support is claimed per exact input profile only after input latency,
debounce/gesture, bus coexistence, reconnect, and long-duration evidence.

### Slice C — runtime configuration and adaptive actions

1. Add target-local binding persistence and reset-only-that-namespace behavior.
2. Expose capabilities and binding validation through settings.
3. Coordinate #170/UHC #333/#335 atomically paired
   `{connection_epoch,generation}` adaptive action-table/binding payloads.
4. Bind current-screen controls to playback/navigation and validated Home
   Assistant actions.
5. Prove a target-local override survives cold restart on the representative M5
   target, and a current-screen action reaches an authorized Home Assistant
   action end to end.
6. Prove unknown, stale, expired, revoked, malformed, offline-disallowed, and
   capability-incompatible action references are rejected, with only an
   explicitly profile-declared adaptive-safe fallback and recovery from
   unavailable accessories; a global media binding must not catch the failure.
7. Prove a queued event/action from an old context, table, connection epoch, or
   generation cannot execute after atomic adaptive snapshot replacement.
8. Extend dependency gates so the binding resolver/action core cannot include
   server-parser or backend-protocol headers.

## Slice A Verification Contract

- Pure value tests cover invalid enum values, bounds, signed extremes, copied
  non-adaptive context snapshots, fail-closed behavior, and rejection of any
  adaptive action/reference while Slice A has no active adaptive action table.
- `_Static_assert` budgets:
  - physical event ≤24 bytes;
  - capability/control descriptor ≤48 bytes;
  - semantic action ≤16 bytes;
  - binding entry ≤40 bytes.
- Resolver tests cover media/picker context snapshots, locked defaults,
  rotation sign/velocity, picker direction, invalid/unknown values, and
  queue-full rejection. Local/server authority tests belong to Slice C.
- Mailbox tests cover Frame safety-latch saturation, Dial full/reverse
  coalescing, counters, and context changes while an event is queued.
- General button gesture and axis dead-zone/hysteresis tests arrive with the
  first driver that consumes those recognizers rather than speculative Slice A
  infrastructure.
- Current behavior characterization proves:
  - Dial rotation still yields signed ±1/±3/±5 semantic volume steps;
  - picker rotation moves exactly one entry per coalesced direction;
  - play/previous/next touch sends the same media command;
  - BLE volume/play-pause preserve their current picker scroll/select behavior,
    while BLE next/previous remain ignored there;
  - BLE media usages retain their current media behavior in settings/recovery
    because those legacy states are non-picker;
  - zone-list tap explicitly selects; zone-label long press opens settings;
  - picker selection is explicit and retains Back/Settings/same-zone/selection
    behavior;
  - BLE usage mapping is identical on Dial and Frame;
  - Frame BOOT/GP4 long presses retain provisioning/restart behavior and PWR
    remains unused.
- Exact include/source dependency policy is updated without wildcard
  exceptions. New modules do not include target renderer or bridge protocol
  headers except the action router's narrow command execution port.
- Native suites and ASan/UBSan pass.
- Clean ESP-IDF v5.5.5 PERF/non-debug builds pass for Dial and Frame with binary
  and map-size comparison.
- Exact hardware candidates require Dial encoder/touch/BLE and Frame
  button/BLE smoke evidence before merge.
- Adversarial checks prove renderer/target/bridge-JSON headers cannot enter the
  resolver, safety bindings cannot be mutated, semantic
  touch/BLE bypass still routes through validation, and a queued physical event
  resolves against the copied non-adaptive context taken at resolution start.

## Program Gates

- Independent solution review and dissent must pass before Slice A execution.
- Independent execute review and dissent must pass on the exact staged diff.
- #216 exact Frame artifact remains frozen and must receive hardware baseline
  evidence independently of this later stack.
- #160 power work begins only after #216 baseline qualification.
- No stacked PR merges without explicit owner approval.
- Final merge candidates must be rebased to current `master`, run the full
  workflow, and publish exact Dial and Frame beta artifacts.

## Solution Review

**Updated:** 2026-07-31

**Initial verdict:** ADJUST

The independent review agreed that the four-layer direction is necessary and
aligned, but found that the first draft was not behavior-preserving:

- BLE volume/play-pause currently scroll/select while the picker is visible;
  direct command dispatch would have silently changed that.
- physical source/default bindings appeared before their owning profile
  descriptors;
- local/server authority and lifecycle were ambiguous;
- one broad hardware slice overlapped #193/#192 and claimed too many unrelated
  M5 protocols at once;
- “Tough-class” was unsafe while the official Tough identity remains non-S3.

All findings are incorporated above: semantic BLE remains context-aware without
becoming fake physical input; Slice A includes only immutable built-in input
descriptors; authority, epoch/generation, bounded tables, and fail-closed
fallback are explicit; #193 owns full target identity; Atom is the sole first
hardware proof; other Units and Chain are separate increments; only verified S3
targets are eligible.

The final re-review returned **CONTINUE**. No remaining solution blocker was
found.

**Final verdict:** ALIGNED / CONTINUE.

## Solution Dissent

**Updated:** 2026-07-31

**Initial decision:** ADJUST, then proceed

The independent dissent steel-manned the architecture but identified the
strongest failure modes:

1. a generic capability/resource/config system could be completed before any
   real accessory falsifies it;
2. renderer queries could survive under a renamed “context” abstraction;
3. queues had no event-class loss/coalescing contract;
4. source identity and Chain persistence were under-specified;
5. a resource registry cannot coexist with private bus creators and honestly
   claim collision ownership;
6. AtomS3's 8 MB/no-Dial-PSRAM assumptions make it a real target proof rather
   than a cloned app;
7. local/server authority and table replacement were ambiguous.

The amended contract narrows Slice A to the two current production anchors,
makes interaction context controller-owned and copied, defines queue classes
and telemetry, makes unproven identities non-persistable, requires the resource
registry to be the sole bus owner, limits the next target to AtomS3 + Atom
Joystick, and makes binding/action publication atomic with an explicit
authority matrix.

The final re-dissent returned **PROCEED** after verifying that:

- Slice A contains no executable adaptive-generation behavior;
- settings/recovery BLE behavior is explicitly preserved;
- M5 Units and Chain are separate post-Atom follow-on candidates;
- rejected adaptive actions can never fall through to a global media binding.

**Final decision:** PROCEED.

## Execute

**Updated:** 2026-07-31
**Status:** implementation and dissent corrections verified locally; final
exact-diff re-review/re-dissent pending

### Pre-flight

- Aim is clear: replace the mixed input enum/legacy binding with a production
  physical-event → contextual-default → semantic-router spine shared by Dial
  and Frame.
- Constraints are known: preserve exact Dial encoder/touch/BLE and Frame
  BOOT/GP4/PWR/BLE behavior; keep effects on the existing controller/UI actor;
  add no task, NVS schema, accessory driver, resource registry, or adaptive
  action table; permit no cross-device state transfer.
- Context is loaded: #220 supplies typed commands and the quarantined picker
  compatibility behavior; #193 owns full target identity; #192 owns Atom
  bring-up; #170/UHC owns adaptive payloads.
- Scope is bounded: Slice A only, with immutable built-in input descriptors,
  copied context, physical rotation/system events, context-aware semantic
  intents, explicit touch navigation, and safety-locked Frame actions.
- Success is explicit: characterization and adversarial native tests pass,
  dependency policy has no wildcard escape, sanitizers pass, both exact
  ESP-IDF 5.5.5 PERF builds pass with measured size, and independent execute
  review/dissent approve the exact diff.

### Implemented

- Replaced the mixed legacy input enum/binding with:
  - bounded physical events;
  - separate event kind, gesture, and bounded transition flags;
  - copied interaction contexts;
  - a bounded target-default binding value and resolver;
  - context-aware semantic control intents;
  - a typed semantic action router.
- Added target-local Dial and Frame input profiles. Each profile owns immutable
  descriptors and default bindings; common resolution no longer hard-codes
  both target maps.
- Preserved Dial rotation acceleration (`±1/±3/±5`), picker direction,
  display wake, wake-tick suppression, and target queue coalescing.
- Preserved BLE media behavior in media/settings contexts and the existing
  picker volume-scroll/play-select/next-previous-ignore behavior.
- Replaced touch's overloaded play/pause selection encoding with explicit
  picker, selection, settings, and transport actions.
- Routed Frame BOOT/GP4 through target-local locked system actions; PWR remains
  described and unbound. Public semantic and ordinary physical dispatch reject
  system actions; the Frame-only ingress requires both an actual target
  descriptor and binding to be safety-locked.
- Replaced Frame's lossy queue with retrying idempotent safety latches.
- Added provisioning readiness distinct from manager startup. It is true only
  after `esp_wifi_start()` succeeds and false during startup, stop, reset, and
  STA/AP transitions, so an early BOOT action remains latched and retries.
- Added accepted/coalesced/dropped/overflow telemetry for semantic controls and
  target input mailboxes. Dial accumulation is saturating and queue overflow is
  logged instead of silently discarded.
- Installed the controller handler before either target starts its input/UI
  actor, closing the startup loss window.
- Updated the exact dependency inventory and strengthened the resolver/input
  rule to cover the new profile/mailbox files and connectivity headers.
- Enabled full CI on stacked PRs, restricted ordinary jobs to read-only
  repository access, and added native plus sanitizer tests using both actual
  target profiles.

### Execute Gap Audit

An independent Terra audit found no P0, but initially returned two P1 findings:

1. Dial silently discarded full-queue rotation events and used non-saturating
   signed accumulation.
2. Frame cleared safety bits before successful dispatch, permitting loss during
   the startup handler window.

It also found P2 gaps in target-local default ownership and mailbox/invalid
value tests. All findings were incorporated: queue telemetry/saturating
coalescing, retrying safety latches, pre-actor controller initialization,
target-owned profile bindings, and adversarial profile/mailbox/value tests.

### Drift Check

- Original aim: establish the current-device physical → contextual binding →
  semantic action spine without adding future accessory/runtime systems.
- Current implementation: the same Slice A spine plus the queue telemetry,
  retry, and target profile values already required by the selected contract.
- Gap: none; no NVS, accessory driver, resource registry, adaptive action
  table, new task, or cross-device migration was introduced.
- Verdict: aligned.

### Local Verification

- Full native workflow suite: PASS.
- Dependency policy and negative fixture: PASS (`243 allowed`,
  `24 grandfathered`).
- Dial and Frame profile contracts: PASS.
- Dial and Frame actual-profile resolution contracts: PASS.
- Frame target-local system integration contract: PASS, including generic
  dispatch rejection and pre-ready latch retry → ready success.
- Mailbox saturation/retry/reverse-motion contracts: PASS.
- Input and action-router invalid/adaptive/context/privileged-action contracts:
  PASS.
- ASan/UBSan for input, mailbox, both profiles, both actual-profile resolvers,
  and router: PASS.
- `actionlint` and workflow YAML parsing: PASS.
- `git diff --check`: PASS.
- Clean ESP-IDF `v5.5.5` target-pinned builds:
  - HiPhi Frame: ESP32-S3, PERF, non-debug, `0x103ed0` bytes
    (`1,064,656`);
    SHA-256
    `2288c094dc12f9cf2ac8f33954e0dabf0ca97fbbcdd1c1b537078e509ae28dac`.
  - HiPhi Dial: ESP32-S3, PERF, non-debug, `0x1b8850` bytes
    (`1,804,368`);
    SHA-256
    `06775c7084a793e6eb461d649bd2dfe14d63a2c12b0b88591abf2ee2e17f8114`.
- Size delta from #220 baseline:
  - Frame: `+0x730` bytes (`+1,840`);
  - Dial: `+0x6d0` bytes (`+1,744`).
- Docker container/image/volume smoke tests and both full target builds
  completed without a new containerd metadata, corruption, or I/O error.

#### ESP-IDF Map Comparison vs. #220

| Target / memory type | #220 used | Slice A used | Delta | Slice A headroom |
|---|---:|---:|---:|---:|
| Frame Flash Code | 757,800 | 759,204 | +1,404 | app partition: `0x2ec130` free (74%) |
| Frame Flash Data | 187,908 | 188,340 | +432 | included above |
| Frame DIRAM | 131,667 | 131,699 | +32 | 210,061 / 341,760 bytes |
| Frame IRAM | 16,384 | 16,384 | 0 | unchanged fixed region |
| Dial Flash Code | 1,139,604 | 1,141,120 | +1,516 | app partition: `0xc77b0` free (31%) |
| Dial Flash Data | 529,268 | 529,492 | +224 | included above |
| Dial DIRAM | 217,967 | 218,015 | +48 | 123,745 / 341,760 bytes |
| Dial IRAM | 16,384 | 16,384 | 0 | unchanged fixed region |

The only internal-memory growth is 32 bytes of Frame BSS and 32 bytes of Dial
BSS plus 16 bytes of Dial data. Executable DIRAM text and the fixed IRAM region
are unchanged. Both application partitions retain the same reported free-space
percentage as #220.

### Execute Review Corrections

The first independent execute review returned **ADJUST** with no P0/P1:

1. artifact evidence predated late validation hardening;
2. actionlint-driven output quoting was safe but not explained as intentional
   workflow hygiene.

The exact current source was rebuilt for both targets and the evidence above
replaces the stale claims. The quoting fixes remain intentionally because they
are required for a clean actionlint gate and also complete the previously
requested CI cleanup; they do not change release semantics.

### Execute Dissent Corrections

The first independent execute dissent returned **ADJUST** and identified five
failure modes:

1. privileged system actions were forgeable through public semantic dispatch;
2. event kind and gesture were conflated;
3. descriptors were not consulted by production resolution;
4. Frame could acknowledge provisioning before Wi-Fi was ready;
5. real target profile behavior was inspected but not resolved in native tests.

All five are corrected:

- semantic dispatch and the action router reject every system action;
- generic physical dispatch also rejects every system action and exposes no
  system-handler registration API;
- the physical value and binding key carry separate kind/gesture plus bounded
  transition flags;
- the resolver validates the matching actual-target descriptor before looking
  up a binding;
- only the target-local Frame input adapter can execute a locked descriptor plus
  locked binding;
- Wi-Fi provisioning readiness is separate from manager startup, and a failed
  pre-ready provisioning attempt re-arms the idempotent latch;
- native and sanitizer tests link and resolve through the actual Dial and Frame
  profile objects; the Frame integration test links the actual target system
  adapter, Frame profile, resolver, and latch primitive.

The dissent also recommended limiting write tokens after broadening stacked PR
coverage. The workflow now defaults to `contents: read` and grants write access
only to release, PR-preview, and Pages jobs.

The final re-review then requested the map-section comparison required by this
contract. The table above records both targets against exact #220 map outputs,
including partition and DIRAM headroom.

### Final Exact-Diff Gates

The final staged binary diff had SHA-256
`9b1c38e9359a9bad2b4ee4db1ad30889c906321160b3d9d1a6583085f6ae0248`.
Both independent Terra gates recomputed that checksum before inspecting it:

- **Review: CONTINUE** — no P0, P1, P2, or P3 findings. It confirmed the
  target-local privilege boundary, distinct Wi-Fi readiness, actual Frame
  adapter/latch integration, complete map comparison, read-only default CI
  permissions, and absence of NVS/cross-device migration or later-slice scope.
- **Dissent: PROCEED** — no P0/P1 bypass or loss condition remained. It
  explicitly stress-tested system-action forgery, physical privilege bypass,
  locked defaults, pre-ready retry, queue saturation/telemetry, target profile
  ownership, stacked-PR CI, and the Slice A boundary.

The dissent retained one non-blocking, pre-existing endurance risk for later
tracking: Dial's hardware driver keeps a cumulative signed raw encoder count.
This slice saturates downstream accumulation and exposes queue loss, but a
continuous one-direction run could eventually overflow that driver-owned
counter. It does not change the current hardware-smoke gate.

### Remaining Execute Gates

- Stacked PR CI and immutable preview artifacts.
- Dial encoder/touch/BLE and Frame BOOT/GP4/BLE hardware smoke before merge.
