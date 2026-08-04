# Shared BLE HID remote host — issue #191

## Session

- Phase: solution-space → execute
- Base: `codex/issue-188-frame-recovery-v2` at `2636acb`
- Branch: `codex/issue-191-shared-ble-hid`
- Dependency: draft PR #216 / issue #188
- Review policy: Terra review and dissent; do not use Sonnet

## Confirmed problem

Owners of compatible ESP32-S3 controllers need optional, consistent support
for pairing a separate BLE HID media remote. Incoming play/pause, next,
previous, volume-up, and volume-down keys must reach the shared controller
without coupling Bluetooth lifecycle or pairing state to a particular display.

The binding constraint is role clarity: Frame's working code is a NimBLE
HOGP **host** that receives keys from a remote. The dormant Dial profile and
documentation describe the opposite role, a BLE HID **device** that advertises
itself as a remote. These roles must not be combined.

Success means the same host service preserves Frame behavior and is available
on Dial, with target-local pairing, persistent enablement, serialized
lifecycle operations, target-owned UI, and Wi-Fi/BLE coexistence evidence from
both exact firmware artifacts.

## Solution space

| Option | Level | Approach | Primary cost |
| --- | --- | --- | --- |
| A | Band-aid | Copy Frame's `ble_remote` into Dial | Two stacks drift and both retain races |
| B | Local optimum | Move the file to `common/` and add UI conditionals | Shares text, not ownership or lifecycle |
| C | Reframe | Shared ESP-IDF HID-host service with narrow target adapters | New component and adapter boundaries |
| D | Redesign | One serialized service task owns all BLE state and commands | Higher initial implementation cost |
| E | Wrong redesign | Unify old Dial HID-device mode with Frame HID-host mode | Conflates opposite roles and stacks |

### Evaluation

#### A — duplicate Frame code

- Solves the stated problem: partially
- Implementation cost: low
- Maintenance burden: high
- Second-order effects: Frame/Dial security, reconnect, and pairing behavior
  diverge immediately.

#### B — lift and shift

- Solves the stated problem: partially
- Implementation cost: low to medium
- Maintenance burden: medium
- Second-order effects: preserves direct `eink_ui` ownership, blocking
  unpair, unsynchronized scan/pair/reconnect operations, and no honest runtime
  lifecycle.

#### C — shared component and adapters

- Solves the stated problem: yes
- Implementation cost: medium
- Maintenance burden: low
- Second-order effects: each target retains a small UI adapter, which is the
  intended display/input boundary.

#### D — serialized owner task

- Solves the stated problem: yes
- Implementation cost: medium to high
- Maintenance burden: low
- Second-order effects: requires explicit asynchronous states, but removes
  the current task races and makes disable/forget behavior testable.

#### E — generic Bluetooth mode

- Solves the stated problem: no
- Implementation cost: high
- Maintenance burden: high
- Second-order effects: mixes NimBLE host and obsolete Bluedroid peripheral
  assumptions, while making neither role clearer.

## Recommendation

Select **C implemented with D's ownership model**.

Create an ESP-IDF-only `rk_ble_hid_host` component. One service task owns the
radio/HID lifecycle and serializes enable, disable, scan, pair, reconnect,
disconnect, and forget commands. NimBLE and `esp_hid` callbacks publish events
back to that owner; public APIs never block an HTTP or UI task.

The service emits semantic media-key and status callbacks. Target adapters map
media keys to `controller_input_post_action()` and marshal status onto their
own UI loops. The component must never include e-ink, LVGL, captive-portal,
bridge, or target-main headers.

This is a HID-host service only. The obsolete, unimplemented Dial HID-device
profile is removed or explicitly quarantined rather than silently reused.

## Required service contract

### State

The externally visible lifecycle is:

`UNAVAILABLE → DISABLED → STARTING → READY → SCANNING | CONNECTING → CONNECTED`

Disable moves any active state through `STOPPING` before `DISABLED`.

Failures enter `ERROR` with a stable error code. Successful disable returns to
`DISABLED`. Status snapshots include enabled state, connection state, bonded
device name, active device name, scan generation/count, and last error.

### Commands

- initialize with target default and callbacks;
- persistently enable or disable;
- start a bounded scan;
- copy a generation-tagged scan snapshot;
- pair a copied device identity, not a mutable array index;
- forget the bonded remote;
- copy a thread-safe status snapshot.

Commands return accepted/rejected immediately. Completion is observable
through status changes. Queue-full, invalid-state, stale-scan, and persistence
failures are surfaced rather than silently ignored.

### Media mapping

The host maps Consumer Control usages for:

- play/pause;
- next track;
- previous track;
- volume up;
- volume down.

Mute remains unsupported until the shared controller has an explicit mute
command. Unknown reports are logged at debug level and ignored safely.

### Lifecycle

- Initialization waits for NimBLE's real sync callback; no fixed readiness
  delay.
- Scan, pair, reconnect, disable, and forget are serialized.
- Disable enters `STOPPING`, invalidates the active generation, and rejects new
  scan/pair/reconnect/enable commands until teardown finishes.
- Teardown cancels scan/reconnect, closes every HID device, waits for all close
  callbacks, frees the devices, and only then deinitializes `esp_hidh`.
- The service task calls `nimble_port_stop()`. The NimBLE host task acknowledges
  that `nimble_port_run()` has returned and its FreeRTOS wrapper has completed;
  only after that acknowledgement may the service deinitialize the port and
  report `DISABLED`.
- A bounded teardown timeout enters a reboot-required, non-reenableable
  `ERROR` state. It must never falsely report `DISABLED`.
- Re-enable starts a fresh stack and reconnects if a bond remains.
- Forget cancels reconnect, closes the device, erases the service metadata,
  and deletes the NimBLE peer security record.
- Every in-flight async operation carries a generation so disable/forget
  invalidates stale callbacks and reconnect attempts.

ESP-IDF 5.5.5 provides the required stop/restart sequence:
`nimble_port_stop()` causes `nimble_port_run()` to return;
`nimble_port_deinit()` releases the host/controller. `esp_hidh_deinit()` is
called only after all HID devices have closed and been freed. Hardware evidence
is still required before claiming repeated runtime toggling works.

## Storage invariants

- Pairing and enablement are device-local NVS only.
- There is no Dial↔Frame import, restore, transfer, reconciliation, or sync.
- Keep Frame's existing local namespace and keys:
  `ble_remote/bonded_bda`, `bonded_atype`, and `bonded_name`.
- Add a local `enabled` key. If absent, Frame defaults enabled to preserve its
  behavior; Dial defaults disabled until the owner opts in.
- Dial uses the same schema in its own independent NVS.
- Do not add BLE pairing state to `rk_cfg_t` or bridge-delivered configuration.
- Do not invent a cross-device or config-blob migration.

## Target adapters

### Frame

- Replace direct `eink_ui` calls with a Frame adapter that posts status on the
  e-ink UI loop.
- Preserve the existing `/ble` pairing experience.
- Preserve current default-on and reconnect behavior.
- Initialize after the first STA connection, as the current target does.

### Dial

- Compile the same host component into the ESP32-S3 target.
- Add a Dial adapter that maps semantic keys to shared controller actions and
  reports status without calling LVGL off-task.
- Add enable/disable, scan, pair, and forget controls to the connected settings
  server; expose connection status in the on-device settings UI where it fits.
- Default disabled for existing and fresh Dial NVS until explicitly enabled.
- Remove the false `CONFIG_BT_ENABLED=n` rationale that claims ESP32-S3 lacks
  the BLE capability needed here.

## Build profile

- Add the shared component to both ESP-IDF projects.
- Host-capable targets depend on `rk_ble_hid_host`; targets that exclude BLE
  depend on the API-compatible `rk_ble_hid_host_stub` component instead.
- Keep the dedicated S3 HID-host Kconfig symbol as a stack/configuration
  assertion for host-capable artifacts.
- Use NimBLE, not the dormant Bluedroid HID-device profile.
- Require `BT_ENABLED`, `BT_NIMBLE_ENABLED`, `BT_NIMBLE_NVS_PERSIST`,
  `BT_NIMBLE_SM_SC`, and `BT_NIMBLE_HID_SERVICE`.
- Fail configuration if the host capability is selected on an unsupported SoC
  or without its required stack features.
- The shipping Dial artifact is host-capable but runtime-disabled when its
  local `enabled` key is absent. Users can opt in without reflashing.
- CI must build the shipping Frame and Dial artifacts with the host component
  present and PERF optimization enabled.
- CI must also compile a BLE-off profile to prove capability exclusion removes
  the NimBLE/`esp_hid` dependency cleanly for targets or builds that omit it.

## Execute slices

1. Add the shared component, pure report mapping, command/state API, local NVS,
   and host tests.
2. Move Frame onto the service through a target adapter without changing its
   user-visible behavior.
3. Enable the component for Dial and add target-owned settings/status surfaces.
4. Remove or quarantine stale Dial HID-device configuration/docs; document the
   HID-host role and LE-versus-Classic distinction.
5. Add CI configuration assertions, build both exact artifacts, and run
   review/dissent before hardware handoff.

These are reviewable commits in one stacked PR for #191. Do not merge it before
#216 lands and the Frame baseline is hardware-proven.

## Verification contract

### Automated

- Pure tests cover report parsing, unknown/release reports, failed-open pointer
  rejection, teardown readiness, stale scan generations, and target-local
  enabled defaults; the disabled profile tests every public stub operation.
- Exact ESP-IDF 5.5.5 PERF builds pass for Frame and host-capable,
  default-disabled Dial.
- A separate BLE-off compile gate passes without the NimBLE/HID-host stack.
- Both generated configs contain the NimBLE/HID-host gates.
- No target UI header is reachable from the shared component.
- No obsolete Bluedroid/GATT-server profile is used.
- Web handlers surface rejected commands and persistence failures.

### Hardware: Frame regression

- cold boot and provisioning remain stable;
- existing local bond reconnects without re-pairing;
- scan, pair, all supported keys, disconnect/reconnect, and forget/reboot work;
- e-ink status changes occur only on the UI loop;
- artwork/state traffic and Wi-Fi recovery continue under BLE load.

### Hardware: Dial capability

- default-disabled boot has no reset loop or behavioral regression;
- enable, scan, pair, key control, reconnect, disable/re-enable, and
  forget/reboot work;
- LVGL remains single-threaded;
- provisioning, bridge polling, artwork, OTA checks, and Wi-Fi recovery remain
  stable under BLE traffic;
- heap and task-stack headroom are recorded from the exact PR artifact.

## Decision gate

Proceed to execute only with:

- Frame PR #216 kept frozen for its current hardware test;
- issue #191 implemented on this stacked branch;
- no claim that ESP32-S3 supports Classic Bluetooth;
- no claim that every BLE remote report format is supported;
- no public beta until exact Frame and Dial artifacts pass their hardware
  contracts.

## Execute

**Updated:** 2026-07-30
**Status:** in-progress

### Pre-flight

- [x] Aim is clear: Frame and Dial share one BLE HID remote-host capability.
- [x] Constraints are explicit: host role only, target-local NVS, no UI
  coupling, no cross-device migration, no Classic Bluetooth claim.
- [x] Current Frame behavior, Dial settings surfaces, ESP-IDF 5.5.5 lifecycle,
  and obsolete Dial peripheral profile have been inspected.
- [x] Scope is bounded to issue #191 on a branch stacked above PR #216.
- [x] Automated and exact-hardware success evidence is defined above.

### Current slice

Implement the shared serialized service and pure tests first. Frame and Dial
adapters follow as separate commits. PR #216 remains frozen for its hardware
test.

## Hardware rejection and linker correction

**Updated:** 2026-07-31
**Integrated test branch:** `codex/issue-190-provisioning-config` / PR #222
**Rejected artifact:** head `a5e73e0`, Actions run `30636774513`

Physical Dial testing reached the BLE settings page but reported
`Unavailable — UNAVAILABLE`. This was not a device setting. Both production
link maps resolved `rk_ble_hid_host_init` from the disabled stub even though
their effective configs enabled the real host.

The cause was two top-level components exporting the same public API. ESP-IDF
auto-discovered both `rk_ble_hid_host` and `rk_ble_hid_host_stub`; unresolved
calls from the target main archive bound to the later stub archive. Existing CI
proved only that Kconfig enabled BLE and that the real source compiled, neither
of which proved which implementation the final ELF selected.

### Execute correction

- Remove the stub wrapper from the production top-level component-discovery
  directory.
- Keep the optional stub wrapper nested under
  `rk_ble_hid_host/optional/rk_ble_hid_host_stub`; BLE-excluding targets must
  opt into that directory explicitly.
- Keep Dial and Frame dependent on the real component.
- Make CI assert the exact `rk_ble_hid_host_init` provider in each final map,
  reject any stub object in production compile/map inputs, and assert the
  inverse for the ESP32-S3 BLE-off fixture.
- Preserve all runtime defaults and NVS behavior: Dial remains opt-in but must
  start in `DISABLED`, never `UNAVAILABLE`; Frame remains default-on.

### Verification

- Dial ESP-IDF 5.5.5 PERF build: PASS, `0x1fb1f0`, 21% app partition free.
- Frame ESP-IDF 5.5.5 PERF build: PASS, `0x146400`, 68% app partition free.
- ESP32-S3 BLE-off minimal build: PASS without NimBLE or `esp_hid`.
- Dial/Frame maps: exact `rk_ble_hid_host_init` provider is
  `librk_ble_hid_host.a(rk_ble_hid_host.c.obj)` and no stub is present.
- BLE-off map: exact provider is
  `librk_ble_hid_host_stub.a(rk_ble_hid_host_stub.c.obj)` and no real host is
  present.
- HID report, policy, and stub tests: PASS.
- Dial identity, controller dependency policy, and negative fixture: PASS.

## Execute review

**Aim:** Ship the shared BLE media-remote host in Dial and Frame rather than an
API-compatible disabled stub.
**Status:** CONTINUE to replacement hardware candidate; not merge/release.

- Necessary: yes — the physically observed Dial behavior contradicted the
  program and #191 contract.
- Aligned: yes — this changes build selection only; it does not add another BLE
  role, schema, UI, or migration.
- Sufficient: yes for the linker defect — the exact final symbol provider is
  now asserted for both production and disabled artifacts.
- Mechanism clear: yes — only one public API provider is discoverable in each
  target's component graph.
- Complete: automated/build proof is complete; real-host coexistence remains a
  hardware gate.

Terra review found no blocker and requested exact symbol/provider matching
rather than a generic object-name grep. The CI checks were hardened accordingly.

## Execute dissent

**Decision under review:** Publish the rebuilt #222 Dial/Frame artifacts as
replacement hardware-test candidates.
**Recommendation:** ADJUST — publish for testing, do not merge or release.

### Contrary evidence and pre-mortem

1. The previous binaries were smaller because the real host was dead code;
   turning it on adds NimBLE/HID code, service stacks, and internal-memory load.
2. Successful compilation and exact map attribution do not prove Wi-Fi/BLE
   coexistence, repeated lifecycle transitions, or callback/task safety.
3. A Dial that leaves `UNAVAILABLE` but resets during enable/scan would still
   fail the user outcome.

### Required hardware evidence

- Dial cold boot reaches `Disabled`, then enable → ready, scan, pair, media-key
  control, reconnect, disable/re-enable, and forget/reboot all work.
- Bridge polling, artwork, settings UI, OTA check, and Wi-Fi recovery remain
  stable while BLE is active; record heap and relevant task stack watermarks.
- Frame separately proves default-on startup, scan/pair/reconnect, keys, e-ink
  UI ownership, and Wi-Fi recovery.

Only exact replacement artifacts may satisfy these gates. Green CI is delivery
integrity evidence, not hardware acceptance.
