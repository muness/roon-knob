# Controller view and command values — issue #190, slice 2

## Session

- Phase: solution-space
- Status: selected after independent review and dissent
- Updated: 2026-07-31
- Baseline: `1492628f196158b33625a8def1503e687bf3a995`
- Branch: `codex/issue-190-controller-values`
- Stack: #216 → #217 → #218 → #219 → this slice
- Agent policy: Terra review/dissent; no Claude/Sonnet or Superego tooling

## Problem

Shared controller code still passes renderer-oriented primitive arguments and
also interprets command-bound input while directly querying picker UI state.
Contributors cannot add another playback backend or adaptive presentation
without extending the mixed `bridge_client` runtime.

The binding constraint is behavior preservation: this slice must remain
releasable on Dial and Frame, add no new task/queue/allocation policy, and must
not implement the adaptive command protocol owned by #170 or the physical-event
and runtime-binding model owned by #194.

Success means production media/connectivity paths carry owned renderer-free view
values, current playback/volume intents carry typed backend-neutral command
values, and the current picker/input coupling is explicitly quarantined behind
a compatibility adapter.

## Solution Space

| Option | Level | Approach | Main trade-off |
|---|---|---|---|
| A. Struct wrappers | Local optimum | Replace long parameter lists but immediately unwrap them in `bridge_client` | Cosmetic; ownership remains mixed |
| B. Values plus compatibility adapters | Reframe | Owned semantic view patches and commands, target/legacy adapters preserve behavior | Transitional adapters remain |
| C. Injected function-table ports | Redesign | Composition root injects view and command sinks | Broad wiring change across the unqualified hardware stack |
| D. Snapshot store, reducer, effects, and queues | Redesign | Durable composite state plus ordered effects and command bus | New scheduling, memory, coalescing, and recovery semantics |
| E. Adaptive-manifest first | Redesign | Let server-defined controls establish the action/view model | Reverses dependencies and duplicates #170 |

### Evaluation

**A — Struct wrappers**

- Solves the primitive argument smell but not the dependency or ownership
  problem.
- Cheap, but likely to become dead code or a second API alongside direct calls.
- Rejected.

**B — Values plus compatibility adapters**

- Establishes compile-time domain values on real production paths.
- Preserves the existing UI-loop serialization and synchronous bridge command
  execution.
- Lets later slices replace adapters without changing the values.
- Selected with the dissent adjustments below.

**C — Injected ports**

- Stronger dependency inversion than B.
- Changes application wiring and lifecycle across both targets before the Frame
  baseline is hardware-qualified.
- Deferred until the values and ownership are characterized.

**D — Snapshot/reducer/effect architecture**

- Best long-term state ownership and replay/coalescing semantics.
- Too broad for a behavior-preserving slice: connectivity and playback coexist,
  effects must remain ordered, optimistic volume needs reconciliation, and
  e-ink backpressure needs explicit policy.
- Deferred; this slice must not imply that a patch is a durable snapshot.

**E — Adaptive first**

- Would prematurely freeze action strings, raw params, and server schema in the
  firmware core.
- Rejected. #170 owns versioned adaptive command payloads.

## Selected Contract

### 1. Owned semantic view patches

Introduce fixed-size, pointer-free values:

- `controller_media_view_t`: primary/secondary/tertiary text, playback flag,
  volume range/value/step, progress, opaque artwork reference, and an artwork
  generation/change signal that preserves same-key forced refresh.
- `controller_connectivity_view_t`: recovery headline and detail only.

These are two distinct patch values rather than a max-sized tagged union:
connectivity and playback coexist, and a small recovery patch must not allocate
the full media value. An outer `controller_view_compat` adapter consumes them
synchronously on the existing UI loop and calls the unchanged
`controller_presentation_*` API. Dial and Frame target presentation adapter
sources therefore remain unchanged in this slice.

The compatibility adapter tracks the last applied artwork reference and
generation. A changed reference is set normally; a generation change with the
same non-empty reference is translated to an explicit clear-then-set sequence
so the unchanged Dial and Frame renderer-level key deduplication cannot suppress
the forced zone-refresh behavior.

Connectivity does not carry fabricated volume or overwrite the most recent
media value. Messages, banners, zone labels, volume overlays, settings, and
battery refresh remain ordered legacy effects in this slice.

### 2. Backend-neutral command values

Introduce a small typed command value for only the existing stable intents:

- toggle playback;
- next track;
- previous track;
- adjust volume by signed semantic steps.

The value contains semantic units, never raw ticks, JSON, URLs, Roon/HQPlayer
names, adaptive action strings, or raw params. The legacy binding converts a
button to ±1 and encoder rotation to the existing signed ±1/±3/±5 velocity
steps. The bridge compatibility path alone owns readiness, clamping, optimistic
display, protocol JSON, error text, and synchronous result semantics.

The bridge emits one canonical `%.10g` absolute-volume number for every typed
volume command. This intentionally normalizes the legacy origin-dependent
formatting: buttons already used `%.10g`, while rotation used `%.1f` and could
send a value different from its optimistic result for sub-decimal zone steps.
The semantic command erases physical origin, and preserving volume precision is
the stable behavior to carry forward.

Volume commands now consistently require an operational target. For direct
volume buttons this intentionally replaces a pre-operational HTTP attempt and
generic failure with the existing `Connecting...` rejection already used by
rotation. Success means the current bridge request completed without its
existing transport/error response; it does not mean asynchronous backend state
has reconciled.

### 3. Explicit legacy input/picker binding adapter

Move the transitional input/picker interpretation out of bridge transport into
an explicitly named compatibility module:

- application startup registers the legacy binding handlers rather than bridge
  transport handlers;
- picker visibility, scrolling, selected-ID queries, Back, Settings, and
  current-zone no-op behavior remain semantic compatibility;
- non-picker transport and volume input maps to `controller_command_t`;
- the adapter uses a synchronous borrowed zone-list visit so it does not add an
  8 KiB zone copy/allocation;
- bridge zone selection returns explicit `found`, `persisted`,
  `became_operational`, and owned label values so the picker continues to
  update/load a found zone even if persistence fails, clears a network banner
  only on the legacy state transition, and the web configuration wrapper
  retains its current failure contract;
- the adapter calls narrow bridge zone/query/command APIs and owns the existing
  picker presentation effects;
- the adapter is owned for retirement by #194.

The new command/view modules must not query target renderer state.

## Dissent Adjustments

Accepted:

- fixed-size owned values; no borrowed strings cross task boundaries;
- patches are distinct from durable composite snapshots and transient effects;
- no new queue or per-field allocation; preserve the existing UI queue and
  allocation count;
- artwork uses an explicit change/generation signal so a same-key zone refresh
  is not lost;
- command execution/result meaning is documented and tested;
- picker behavior is quarantined in a named legacy adapter;
- exact field maxima and `_Static_assert` budgets are gates:
  media patch ≤576 bytes, connectivity patch ≤192 bytes, command ≤8 bytes;
- each queued patch replaces the existing media/connectivity allocation rather
  than supplementing it; allocation count and queue ownership are unchanged;
- existing renderer-to-bridge artwork URL queries remain explicit debt rather
  than being misrepresented as fixed.

Deferred because they change runtime semantics:

- durable composite snapshot storage, validity/revision merging, stale-update
  rejection, and latest-wins coalescing;
- a separate typed transient-effect bus;
- optimistic-volume rollback/reconciliation and multiple-pending-command
  sequencing;
- artwork download retry ownership and the narrow artwork-source port;
- reducer/event sourcing or function-table port injection.

## Implementation Scope

1. Add pure owned view and command headers with `_Static_assert` size budgets.
2. Route media and recovery/connectivity updates through the two view values on
   the existing UI task.
3. Add an outer compatibility adapter that translates values into the unchanged
   Dial/Frame presentation API.
4. Add the legacy input/picker binding adapter and register it from `app_main`.
5. Add bridge APIs that accept typed commands; remove input types and picker
   queries from bridge transport.
6. Isolate bridge readiness, clamp, optimistic-volume, and JSON planning in a
   pure compatibility helper so the existing command semantics are directly
   characterized without a transport test hook.
7. Update the exact dependency policy without wildcard allowances; any new
   compatibility violation is owned by #194.

## Tests

- View values are owned, bounded, null-terminated, and within the declared byte
  budgets; each production queue post replaces the prior allocation.
- The view compatibility adapter forwards every media scalar/string, does not
  fabricate media state for connectivity, and gates artwork refresh correctly;
  Dial and Frame stubs prove changed-key set, unchanged no-op, and same-key
  generation clear-then-set; existing target adapter characterization remains
  green.
- Every supported legacy input maps to the exact typed command; `NONE`, unknown,
  and navigation-only inputs fail closed.
- Picker open/scroll/back/settings/same-zone/select-zone behavior remains
  characterized through stubs, including found-but-not-persisted selection.
- Volume action and rotation paths preserve readiness, velocity, clamp,
  optimistic overlay, canonical precise JSON formatting, and failure messages;
  the pure bridge plan covers rejection, no-op, clamping, sub-decimal steps,
  extreme signed steps, exact action JSON, and fail-closed command kinds.
- Dependency policy passes and explicitly records any new compatibility edges.
- Native tests, clean ESP-IDF v5.5.5 Dial/Frame builds, PERF/non-debug checks,
  and binary/map-size comparison pass.

## Explicit Exclusions

- No adaptive action strings, params JSON, manifest elements, or protocol
  versioning (#170).
- No new physical-event enum, runtime bindings, accessory driver work, or final
  removal of `controller_input_action_t` (#194).
- No durable snapshot store, effect bus, queue/coalescing policy, or new task.
- No bridge transport extraction, directory moves (#174), or renderer rewrite.
- No artwork URL/source-port extraction; its two owned forbidden edges remain.
- No configuration/NVS schema change, device-to-device transfer, or migration.
- No change to the exact #216 Frame artifact or #160 power behavior.

## Gates

- Independent solution review and dissent contracts are posted to issue #190.
- Execute review and dissent must pass on the exact staged diff.
- Both embedded targets build from clean IDF v5.5.5 state.
- Final stack must be rebased to `master` and receive the full workflow.
- Production presentation/command changes require Dial and Frame exact-candidate
  hardware checks before merge; #216 baseline and #160 power evidence remain
  separate open gates.

## Execute

**Updated:** 2026-07-31
**Status:** implementation verified; exact clean rebuild and execute
review/dissent passed

- Aim is clear: establish real renderer-free view and backend-neutral command
  values on production paths.
- Constraints are known: preserve the current UI task, queue, allocation count,
  target presentation sources, picker semantics, NVS, and server protocol.
- Context is loaded: #219 fixes the observed dependency graph; #170 owns
  adaptive payloads; #194 owns final physical-event/runtime binding.
- Scope is bounded to the two view patches, outer view compatibility, semantic
  commands, legacy input/picker binding, narrow zone/command bridge APIs,
  dependency updates, and characterization.
- Success is explicit: production paths use the values, native contracts pass,
  allocation and struct-size budgets hold, dependency enforcement passes, both
  clean ESP32-S3 targets build, and independent execute review/dissent pass.

### Implementation evidence

- Production media and connectivity posts now transfer owned
  `controller_media_view_t` and `controller_connectivity_view_t` values on the
  existing UI queue. No task, queue, or additional production post was added.
- `controller_view_compat` preserves unchanged target adapters and translates a
  same-reference artwork generation change to clear-then-set.
- `controller_command_t` is backend-neutral and pointer-free; the legacy
  binding alone maps current input/picker behavior and is explicitly owned for
  retirement by #194.
- Bridge execution owns operational rejection, clamp, optimistic volume,
  protocol JSON, and error feedback. `bridge_command_plan` makes those rules
  pure and directly testable.
- Zone selection now returns
  `{found, persisted, became_operational, zone_name}`. The picker keeps
  found-but-not-persisted feedback and clears a network banner only when the
  selection transitions to operational, before the picker closes as in the
  baseline; the web wrapper still fails when persistence fails.
- Native contracts pass under `-Wall -Wextra -Werror -pedantic`: controller
  values, legacy binding, bridge command planning, existing input,
  Dial/Frame presentation, config, and BLE HID suites.
- Exact include/source inventory passes with 243 observed edges:
  216 allowed and 27 explicitly grandfathered. The only new forbidden
  dependency is the Dial/Frame legacy binding's picker-presentation query,
  owned by #194; the adversarial CMake/include self-test passes.
- Exact-final-diff clean ESP-IDF v5.5.5 builds succeeded for both targets with
  PERF enabled and DEBUG absent. Current binaries are `0x1b8180` (Dial,
  +`0x170` versus the pre-slice baseline) and `0x1037a0` (Frame, +`0x90`).

## Review

**Updated:** 2026-07-31

**Verdict:** ALIGNED / PASS

**Aim:** Establish owned controller views and typed commands on real Dial/Frame
paths without changing target adapters or losing legacy interaction semantics.

- Necessary: yes; these are production seams required by Frame, adaptive UI,
  and additional targets.
- Aligned: yes; media/recovery and command paths use the new values.
- Sufficient: yes; compatibility and legacy binding remain bounded, with no
  new runtime infrastructure.
- Mechanism clear: yes; owned values cross the existing UI task, the outer
  adapter preserves target implementations, and bridge planning owns protocol
  semantics.
- Complete for this slice: yes; source inventories, CI, native contracts,
  sanitizer runs, exact clean target builds, and dependency enforcement pass.

The initial review requested two adjustments. Canonical `%.10g` volume
serialization is now an explicit precision correction with fractional-step
coverage. Zone selection now carries `became_operational`, restoring the
baseline transition-only banner clear before picker close across all
persistence/transition combinations. No actionable defect remains.

## Dissent

**Updated:** 2026-07-31

**Decision:** PROCEED

The strongest contrary evidence was:

1. an operational zone switch could have hidden a useful retry banner;
2. unified commands changed rotation from lossy `%.1f` to precise `%.10g`;
3. the borrowed zone visit holds the bridge lock across synchronous picker
   presentation.

The first is fixed and characterized with an explicit transition result. The
second is consciously accepted and tested as a semantic precision correction,
using the format already used by legacy volume buttons. The third remains a
prominent transitional #194 no-re-entry constraint; current target adapters do
not re-enter bridge state. Remaining target hardware qualification is a release
gate, not an implementation defect in this slice.
