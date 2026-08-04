# Incremental controller boundary enforcement

**Date:** 2026-07-31
**Status:** Accepted for the first #190 execution slice
**Issue:** #190

## Context

The Dial and recovered Frame now share controller input and presentation seams,
but the broader source graph still mixes backend protocol, controller state,
commands, configuration, connectivity, recovery, rendering, and target
hardware. A directory rewrite would hide rather than reduce this risk.

This decision distinguishes the architecture we want from the graph that exists
today. It creates a ratchet: existing debt is explicit and owned, while new
direct source coupling fails CI.

## Target dependency direction

```text
target display, input, BLE, power, and provisioning drivers
                              |
                              v
target composition root -> normalized input -> controller commands
                              |                    |
                              |             controller runtime
                              |      state, recovery, configuration ports
                              |                    |
                              +-- presentation <- semantic controller view
                                                   |
                                            backend adapter
```

The dependency rules are:

1. Controller state and command code does not include LVGL, e-ink, GPIO, BLE
   implementation, captive-portal, or target application headers.
2. Backend adapters translate transport protocols into typed controller state
   and commands. They do not include target input or renderer APIs.
3. Presentation adapters consume controller updates and call one target
   renderer. Renderers do not fetch backend state directly.
4. Input drivers emit normalized physical events. Binding events to commands
   belongs above the driver.
5. Connectivity publishes events and accepts narrow configuration mutations; it
   does not render UI directly.
6. A configuration service owns in-memory mutations. Target storage persists
   only that device's local configuration. No target imports another target's
   NVS namespace, blob, settings, or bonds.
7. Target composition roots are the only layer that assembles display, input,
   BLE, power, provisioning, controller, and backend adapters.
8. Privileged system actions have no semantic or generic physical dispatch
   path and no common handler-registration API. They execute only through the
   target-local Frame input adapter after the actual profile resolves both a
   safety-locked descriptor and a safety-locked binding.
9. Wi-Fi manager startup and provisioning readiness are distinct. A Frame BOOT
   request remains in the retrying safety latch until ESP Wi-Fi can accept the
   STA-to-AP transition.

## Observed graph and API disposition

The first two seams are useful but transitional:

| Surface | Disposition | Reason |
|---|---|---|
| `controller_input_post_control` and UI/controller queue dispatch | retained | Cross-task semantic input is resolved and effects run on the target actor |
| Pointer-free physical events, copied interaction context, and semantic actions | retained | Drivers, contextual defaults, and effects now have distinct values; #194 later adds persisted/runtime bindings |
| `controller_action_router` picker effects | transitional | It owns current semantic picker behavior without renderer visibility queries; adaptive actions remain #170/#194 Slice C |
| `controller_presentation` target selection | retained | Link-time Dial/Frame adapters remove renderer conditionals from shared controller code |
| Zone-picker visibility, scrolling, and selected-ID queries | transitional | They model the current imperative picker, not the future semantic adaptive view |
| Dial LVGL and Frame e-ink implementations | target-owned | Pixel layout, refresh cadence, and target state remain outside the controller |
| `bridge_client` | transitional mixed runtime | It currently owns protocol, state, commands, configuration, polling/recovery, and presentation scheduling |

Known reverse or mixed-responsibility edges are recorded individually in
`scripts/controller_dependency_policy.json`. The policy has no wildcard or
directory-level exception. Every transitional edge names an issue and a
retirement condition.

## Enforcement boundary

The first slice enforces direct quoted C header includes only:

- every repository-local quoted include reachable from each target's literal
  `SRC_FILES` inventory is an exact checked edge;
- an unlisted new edge fails;
- a removed edge leaves a stale manifest entry and fails until the exception or
  allowance is retired;
- known forbidden directions are machine-classified, so they cannot be added as
  ownerless allowances;
- unsupported `SRC_FILES` mutation, unresolved repository includes, local
  angle-bracket includes, and macro includes fail instead of being skipped;
- the CMake contract requires `SRCS ${SRC_FILES}` exclusively and rejects
  every command outside the current manifest allowlist, including inline
  sources, `list(APPEND ...)`, `target_sources(...)`, `add_subdirectory(...)`,
  and `include(...)` indirection, and rejects ESP-IDF `SRC_DIRS` or
  `EXCLUDE_SRCS` source mutation;
- the only accepted `set` and `set_property` forms are the literal source
  inventory and the two existing non-source build properties;
- adversarial fixtures prove rejection of a previously unknown target header,
  comment-separated and line-spliced include tokens, an ownerless forbidden
  edge, and seven compiled-source bypass forms.

This does **not** claim IDF component-level or link-level separation. Both
targets still compile shared and target files in one component. Later #190
slices may strengthen the check after the source graph is split. External
angle-bracket includes, preprocessor condition semantics, symbols, and runtime
calls remain outside this source-include contract.

Stacked PRs target their predecessor rather than `master`, so the workflow runs
for pull requests to every branch. Ordinary test/build jobs have read-only
repository access; write permission remains scoped to release, preview, and
Pages jobs. The final rebased merge candidate must still run the full workflow
against `master` and pass before merge.

## Characterization boundary

Native tests freeze externally useful semantics:

- physical event/context resolution through the actual Dial and Frame profiles;
- rejection of unknown gesture/flag/value combinations and forged or unlocked
  privileged system actions;
- semantic BLE/control-intent behavior in media, picker, and
  settings/recovery contexts;
- rejection of invalid, unbound, and unsupported adaptive values;
- UI queue FIFO order, effective capacity, and full-queue failure;
- retrying Frame safety latches, including provisioning before Wi-Fi readiness;
- the actual Frame system adapter rejecting generic ingress, propagating
  pre-ready failure, and executing only locked BOOT/GP4 profile actions;
- explicit picker open/scroll/select/close/settings effects and context
  transitions;
- Dial and Frame presentation adapters forwarding semantic values to their
  target renderer;
- intentional target differences, such as Dial ignoring `line3` and Frame's
  simplified zone-picker entry.

Tests must not freeze pixels, e-ink refresh duration, thread sleeps, or
incidental internal call sequences. Hardware validation remains separate.

## Migration sequence

1. **This slice:** decision, exact direct-include ratchet, negative fixture, and
   input/presentation characterization. Keep #190 open.
2. Introduce renderer-free controller view and command value types behind
   compatibility adapters.
3. Characterize and centralize configuration mutations and connectivity events;
   remove stale whole-struct ownership.
4. Extract backend transport from the mixed `bridge_client` runtime.
5. Replace imperative picker queries with semantic navigation/view state.
6. Let #170 and the server contract own versioned adaptive command payloads.

## Related issue disposition

- #132 and #148 are evidence for scheduler and UI cleanup, not prerequisites or
  architecture templates.
- #170 owns versioned adaptive command dispatch.
- #174 alone owns source-directory moves.
- #188 supplies the Frame characterization anchor.
- #191 supplies a shared BLE input consumer.
- #194 owns configurable action bindings and the eventual separation between
  physical events and command meaning.

## Consequences

The policy requires an intentional manifest update when a legitimate direct
edge changes. This is deliberate review friction. It prevents new debt but does
not pretend existing debt has been removed, and it keeps every intermediate
step buildable and releasable.
