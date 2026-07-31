# Controller configuration ownership and provisioning boundary — issue #190

## Session

- Phase: execute
- Status: implementation accepted for draft push; CI and exact-artifact hardware gates remain
- Updated: 2026-07-31
- Baseline: `bdea8e6f4ebfe555416667a9a928a632c3abacb0`
- Branch: `codex/issue-190-provisioning-config`
- Stack: #216 → #217 → #218 → #219 → #220 → #221 → this slice
- Coordinates with: #188, #190, #191, #194, and #198
- Agent policy: Terra review/dissent; no Claude/Sonnet or Superego tooling

## Confirmed Problem

The firmware now has typed controller values, presentation sinks, semantic
actions, and normalized input seams, but configuration and provisioning still
have multiple owners:

- `app_main` loads/defaults/saves the complete `rk_cfg_t` at boot;
- `wifi_manager` independently loads, defaults, caches, and saves another
  complete `rk_cfg_t`;
- `bridge_client` keeps a third complete copy and persists bridge, zone, and
  server-delivered display/power changes under its own state lock;
- both target captive portals load and save the complete blob directly;
- connected settings partly route Wi-Fi and bridge edits through
  `bridge_client`, making an integration client the accidental transaction
  owner;
- shared `wifi_manager.c` includes either Dial or Frame `captive_portal.h`
  directly.

This makes a valid field-local edit capable of overwriting unrelated newer
state. It also prevents the shared connectivity layer from compiling without a
specific target portal.

Success means:

1. one shared owner serializes the current device's configuration and all
   complete-blob persistence;
2. callers mutate only the fields they own through bounded APIs;
3. the bridge integration no longer owns device persistence;
4. shared Wi-Fi code requests provisioning through a target-neutral port;
5. Dial and Frame retain their target-local HTTP/UI behavior;
6. the exact NVS namespace, key, blob layout, defaults, and same-device
   V1/V2/V3 upgrade behavior remain compatible;
7. Wi-Fi retry, AP transitions, bridge discovery, recovery presentation, BLE,
   display sleep, and power behavior do not change in this slice.
8. compiled default SSID/password behavior remains identical on empty NVS;
9. committed-but-unverifiable NVS writes cannot leave runtime state knowingly
   older than the candidate that may be durable;
10. stale mDNS or server responses cannot overwrite a newer endpoint decision.

This is explicitly **not** migration between Dial, Frame, or any future target.
Each device retains only its own NVS. No code will import, translate, restore,
or transfer configuration from another device.

## Situated Evidence

### Persistence ownership

- `rk_cfg_t` is a 570-byte V3 compatibility blob containing local Wi-Fi,
  bridge/zone selection, server display preferences, and power settings.
- Dial and Frame use the same `rk_cfg` namespace and `cfg` key in separate
  device-local NVS stores.
- Both target storage implementations already perform same-device V1/V2 → V3
  upgrades and repair malformed V3 Wi-Fi fields.
- `wifi_manager`, not the storage defaults function, currently applies
  `CONFIG_RK_DEFAULT_SSID/PASS` on first boot. Moving initialization without
  moving that policy would regress compiled-credential devices into AP mode.
- `bridge_client` serializes only its own writes. A portal can still load an
  older blob and later save it over a newer bridge-owned value.
- `rk_cfg_copy_local_connectivity()` proves that field-scoped replacement can
  preserve bridge, zone, display, and power fields, but only callers routed
  through `bridge_client_store_local_connectivity()` receive that protection.
- `platform_storage_save()` remains reachable from boot, Wi-Fi, both portals,
  and the bridge client.
- Both storage backends commit before read-back verification. A `false` return
  can therefore mean either "not committed" or "committed but verification
  failed"; a Boolean cannot promise rollback.
- BLE enablement and bonds live in the BLE component's separate NVS state, not
  in `rk_cfg_t`, and are outside this owner.

### Provisioning ownership

- `wifi_manager` owns STA/AP transitions, retry/backoff, and `rk_net_evt_t`.
- Dial and Frame captive portals own different HTTP endpoints and renderer
  behavior.
- `common/wifi_manager.c` directly includes each target's
  `captive_portal.h`; both forbidden edges are grandfathered to #190.
- Starting and stopping the captive portal is the only target operation that
  shared Wi-Fi lifecycle code needs.

### Recovery and lifecycle

- `app_entry()` runs before `wifi_mgr_start()` on both targets.
- `wifi_manager` owns Wi-Fi retry/AP state; `bridge_client` owns bridge retry,
  mDNS discovery, and operational state.
- Target `rk_net_evt_cb()` functions translate network events into target
  presentation and deferred service startup.
- Those recovery paths are behaviorally important but do not need to be
  redesigned to establish configuration and provisioning ownership.

## Solution Space

The candidates were generated before evaluation.

| Option | Level | Approach | Main trade-off |
|---|---|---|---|
| A. Deduplicate NVS backends | Local optimum | Move the duplicate Dial/Frame NVS implementation into one shared ESP-IDF source | Removes duplication but leaves every competing writer intact |
| B. Make `bridge_client` the config owner | Local optimum | Route every portal/Wi-Fi edit through new bridge-client methods | Smallest diff, but turns an integration adapter into a device god object |
| C. Dedicated config owner plus provisioning port | Reframe | One shared config transaction service; target adapters implement portal lifecycle | More call-site migration, but establishes the intended dependency direction |
| D. Split the NVS schema now | Redesign | Persist Wi-Fi, bridge, zone, display, and power as separate records | Strong isolation at the cost of unnecessary schema migration and rollback risk |
| E. Extract a connectivity/recovery reducer first | Reframe | Normalize Wi-Fi/bridge events and retry state before persistence | Valuable later, but leaves the stale-writer bug and target include inversion |

### A — deduplicate NVS backends

- Solves the stated problem: no. It creates one backend implementation but not
  one owner.
- Implementation cost: low.
- Maintenance burden: lower duplication, unchanged lifecycle ambiguity.
- Second-order effects: direct full-blob writers remain legal and can still
  clobber each other.
- Optionality: useful follow-up after ownership is enforced.
- Rejected as this slice's solution.

### B — make `bridge_client` the config owner

- Solves the stated problem: partially.
- Implementation cost: low to medium.
- Maintenance burden: high because Wi-Fi boot, captive setup, zone selection,
  remote preferences, and persistence all become bridge concerns.
- Second-order effects: provisioning before bridge readiness becomes awkward;
  future HQPlayer/Home Assistant adapters would depend on a Roon-named owner.
- Optionality: poor for backend-neutral controller factoring.
- Rejected.

### C — dedicated config owner plus provisioning port

- Solves the stated problem: yes.
- Implementation cost: medium and independently releasable.
- Maintenance burden: low if the owner performs transactions and persistence
  only, while Wi-Fi, bridge, renderer, and recovery actors keep their effects.
- Second-order effects: requires typed projections/mutations, explicit startup,
  and characterization of failed persistence.
- Optionality: high. Future targets and integrations consume a stable device
  config boundary without learning NVS or a target portal.
- Selected.

### D — split the NVS schema now

- Solves the stated problem: technically, but by changing storage instead of
  fixing ownership.
- Implementation cost: high.
- Maintenance burden: multiple versioned records and rollback rules.
- Second-order effects: creates an unnecessary same-device migration and risks
  credentials during beta recovery.
- Optionality: low until every old firmware downgrade path is addressed.
- Rejected.

### E — extract connectivity/recovery first

- Solves the stated problem: no. Event ownership improves but persistence can
  still lose unrelated fields.
- Implementation cost: medium to high because current target wording, timers,
  deferred service startup, and bridge thresholds differ.
- Maintenance burden: good eventually.
- Second-order effects: changes the most hardware-visible recovery path before
  its inputs/configuration are stable.
- Optionality: good as the next #190 slice.
- Deferred.

## Recommendation

Select **C: a dedicated controller configuration owner plus a target-neutral
provisioning port**.

The configuration owner is not a new god controller. It owns only:

- initialization of the current device's existing compatibility blob;
- a synchronized in-memory snapshot;
- field-scoped validation and mutation;
- atomic persistence and failure reporting.

It does not own Wi-Fi effects, HTTP, bridge polling, presentation, BLE, sleep,
power, recovery state, or backend commands.

### Configuration lifecycle

Add a shared `controller_config` module initialized once by `app_entry()`:

1. load/decode through `platform_storage` without allowing the loader to save;
2. accept the storage backend's existing same-device upgrade/repair result and
   persist it through the owner when required;
3. apply the existing storage/display defaults when no valid blob exists;
4. then apply the current `CONFIG_RK_DEFAULT_SSID/PASS` fallback and primary
   Wi-Fi normalization in the same order used today;
5. persist the resulting candidate through the bounded write-result contract;
6. publish a copied snapshot, durability state, and monotonic runtime revision
   for startup consumers;
7. serialize every later mutation and complete-blob save under one lock.

`bridge_client_start()` and `wifi_mgr_start()` consume snapshots from this
owner. Neither loads or saves NVS. `bridge_client` must not retain authority to
persist a complete `rk_cfg_t`.

The compatibility blob may remain the internal storage DTO in this slice. It
must not become the public mutation API.

`platform_storage_load()` must no longer persist normalization or migration as
a side effect. It returns decoded data plus whether owner initialization must
persist it. The unused public `platform_storage_reset_wifi_only()` bypass is
removed or made backend-private; factory reset remains a separate recovery
effect with unchanged semantics.

Every production read also moves to an owner snapshot or typed projection.
Target UI, OTA, settings, portals, Wi-Fi, and bridge code cannot load raw
storage. The only raw storage users are the owner and target backend
implementation used on its behalf. The existing device-local factory-reset
effect remains an explicit recovery exception because it erases NVS rather than
reading or mutating the compatibility blob.

### Persistence outcomes

Replace Boolean write semantics with a bounded result:

- `NOT_COMMITTED` — validation/write/commit failed before durability can be
  claimed; do not publish the candidate;
- `COMMITTED_VERIFIED` — commit and read-back verification succeeded; publish
  the candidate and allow the caller's existing success effect;
- `COMMITTED_UNVERIFIED` — commit succeeded but read-back was unavailable or
  mismatched; publish the candidate to avoid a knowingly stale RAM/new-on-reboot
  split, return a degraded diagnostic, and do not present the operation as
  verified success.

The owner and target surfaces must distinguish these outcomes. A committed-
unverified result may be applied to runtime consumers for consistency, but a
portal must not show its normal verified-success page or automatically reboot.
It keeps the recovery surface available and reports that verification failed.

`COMMITTED_VERIFIED` requires equality for **every persisted V3 field** after
normalization on both targets. Dial and Frame use the same common full-candidate
comparison; SSID-only or selected-field verification is forbidden. The check
must not depend on indeterminate struct padding: it compares every named stored
field or a canonical zero-filled V3 encoding. Raw compatibility fixtures still
prove the total stored blob remains 570 bytes with unchanged field order.

### Initialization durability state

Initialization publishes one of these runtime-only states:

- `DURABLE` — an existing valid blob loaded, or a new/default/normalized
  candidate was committed and fully verified;
- `DEGRADED_COMMIT` — a candidate was committed but full read-back verification
  failed; publish that candidate and allow Wi-Fi/bridge startup so RAM is not
  knowingly older than a possibly durable value;
- `VOLATILE_RECOVERY` — storage could not load or commit; publish synthesized
  existing defaults plus the compiled Wi-Fi fallback as a non-durable recovery
  snapshot so Wi-Fi/AP provisioning can still start.

All three publish a valid copied snapshot before `bridge_client` or
`wifi_manager` starts. The latter two also publish a configuration-storage
diagnostic through the existing target presentation path:

- degraded commit: "Settings saved but could not be verified";
- volatile recovery: "Settings storage unavailable; changes may not survive".

Neither condition is represented as durable. Normal Wi-Fi/bridge startup
continues from the published snapshot; an empty volatile snapshot reaches AP
provisioning, while compiled credentials retain the current STA-first behavior.
Subsequent verified transactions advance the durability state to `DURABLE`.
There is no boot loop or automatic erase.

### Field ownership and mutations

The shared owner exposes bounded operations for:

- local connectivity:
  - list/snapshot saved networks without exposing mutable internal storage;
  - add/update a network;
  - explicitly promote a network to first connection priority;
  - remove a network;
- controller endpoint:
  - replace bridge base and discovery-source flag together;
- zone:
  - replace selected zone ID;
- remote controller preferences:
  - atomically merge only an explicit server-owned
    `controller_remote_preferences_t` projection containing name, config SHA,
    rotation, art-mode, dim, sleep, deep-sleep, and the three existing power
    fields.

Every operation:

1. validates inputs before taking effect;
2. mutates a private candidate copied from the current authoritative value;
3. persists the candidate;
4. publishes it only for a committed outcome;
5. returns the exact persistence outcome and copied committed snapshot;
6. never calls Wi-Fi, bridge, UI, or another consumer while holding the config
   lock.

The owner may expose a transitional full copied snapshot for read-only
consumers. No caller receives a mutable pointer, a lock callback, or a generic
"edit any `rk_cfg_t` field" transaction.

Remote preferences use explicit presence bits so an omitted field preserves the
current value. The complete candidate is validated before commit:

- name and SHA must be terminated and bounded by their existing fields;
- rotation is one of `0`, `90`, `180`, or `270`;
- every enabled flag is Boolean;
- timeout and polling values fit the existing stored types and documented
  product bounds;
- the projection contains no Wi-Fi, bridge endpoint/source, zone, BLE, OTA,
  binding, recovery, HTTP, mDNS, or presentation field.

A malformed or out-of-range payload is rejected as a whole. It causes no
commit, publication, display/power application, or config-SHA advance.

### Snapshot propagation and stale-response fencing

There is one authoritative configuration snapshot: `controller_config`.
`wifi_manager` and `bridge_client` may retain derived runtime state needed for
their actors, but they do not own a persistable full blob.

After a committed mutation, the caller passes the returned copied projection to
a narrow apply method:

- Wi-Fi receives local connectivity only; its apply method never persists and
  reconnects only where current behavior reconnects;
- bridge receives endpoint/zone changes needed for polling only; its apply
  method never persists;
- display/power receives remote preferences only after the config transaction
  commits.

No generic observer list or callback-under-lock is introduced. Lock order is:
finish the config transaction and release its lock, then acquire an actor lock
to apply the copied result. Actor code never calls a config mutation while
holding its own lock.

The owner also maintains an in-memory endpoint generation. Capturing an
endpoint token copies the generation and endpoint/source:

- mDNS may set a discovered endpoint only if the token is still current and the
  endpoint is still clear;
- a fetched remote-preference response may commit only if the endpoint token
  used for the request is still current;
- manual set or clear advances the endpoint generation;
- default-endpoint fallback, manual-clear rediscovery, periodic rediscovery, and
  every other endpoint writer use the same token/conditional operations;
- unrelated Wi-Fi, zone, or preference transactions do not invalidate an
  endpoint token.

This fencing metadata is runtime-only and does not change NVS layout.

### Provisioning port

Add a shared platform interface with the minimum lifecycle required by
`wifi_manager`:

- start the target's AP captive portal and return verified ready/failure;
- stop the target's AP captive portal.

Dial and Frame each implement that interface by calling their existing portal
code. Portal HTTP endpoints, HTML, display messaging, STA-only settings
servers, and target renderer calls remain target-local.

`common/wifi_manager.c` includes only the shared provisioning interface. The
two grandfathered `common/wifi_manager.c → */captive_portal.h` edges are removed
from the dependency inventory.

On success, ordering remains: start Wi-Fi AP → start and verify the target
portal → emit `RK_NET_EVT_AP_STARTED`. Stop remains idempotent.

If portal start fails, `wifi_manager` must not emit `AP_STARTED` or present
setup as ready. It stops any partial portal and keeps the Wi-Fi AP transition
stable while a **separate provisioning-service retry timer** retries only the
portal start with capped exponential backoff. It does not cycle through STA
credential retry and cannot create an AP/STA livelock. Each failure emits an
explicit "Setup service unavailable; retrying" diagnostic; success emits
`AP_STARTED` exactly once. AP stop cancels the timer and remains idempotent.
Tests use an injected failing adapter to prove rate-bounded retries, no false
ready state, eventual success, and stop cancellation. The normal success path
and its target presentation remain unchanged.

### Recovery behavior

This slice characterizes and preserves:

- first boot with empty/default Wi-Fi;
- STA connect/retry and promotion order;
- fallback into setup AP;
- AP stop and STA reconnect;
- `RK_NET_EVT_*` delivery;
- bridge ready/unready transitions;
- mDNS/manual bridge source behavior;
- current factory-reset behavior.

It does not consolidate Wi-Fi and bridge retry state, change thresholds, or
normalize target recovery copy. Those become the next connectivity/recovery
slice after configuration ownership is proven.

## Execute Contract

The production change is one **atomic cutover**. Internal commits may build the
owner and migrate call sites in stages, but no intermediate state with two
complete-blob owners is eligible to merge, release, or provide beta firmware.

The cutover includes:

1. native owner/storage-outcome tests and the implementation;
2. boot/default policy migration;
3. removal of the bridge and Wi-Fi full-blob copies as persistence authorities;
4. migration of every production writer to typed owner operations;
5. provisioning port/adapters and removal of common-to-target portal includes;
6. stale endpoint/server-response fencing;
7. source/dependency enforcement against owner bypass;
8. full native/sanitizer/target build verification and immutable previews.

The source policy fails when code outside `controller_config` or a backend:

- calls a complete-blob save;
- invokes a storage mutation/reset helper;
- receives mutable storage data;
- includes a target storage implementation.

Raw storage decode/write APIs become owner-private by dependency policy even
when C linkage remains necessary for the platform backend.

The atomic cutover is the independently buildable and releasable #190 migration
step. Enforcement and artifact evidence are gates on that step, not a later
behavioral release.

## Operation and Effect Matrix

| Operation | Commit owner | Runtime effect after commit | Verified-success behavior | Degraded/not-committed behavior |
|---|---|---|---|---|
| Existing valid config | `controller_config_init` | Publish `DURABLE` snapshot before bridge/Wi-Fi start | Current STA/AP path | N/A |
| First boot/defaults, verified | `controller_config_init` | Publish `DURABLE` snapshot before bridge/Wi-Fi start | Current compiled-SSID or AP path | N/A |
| First boot/defaults, committed-unverified | `controller_config_init` | Publish candidate as `DEGRADED_COMMIT`; allow bridge/Wi-Fi start | N/A | Surface verification diagnostic; no false durable claim or boot loop |
| Storage load/commit unavailable | `controller_config_init` | Publish defaults/compiled Wi-Fi as `VOLATILE_RECOVERY`; allow bridge/Wi-Fi/AP start | N/A | Surface non-durable diagnostic; empty credentials still reach setup AP |
| Captive add/update Wi-Fi | local-connectivity mutation with promote | Update Wi-Fi derived cache after config lock | Existing success/countdown/reboot | No normal success or auto-reboot; keep portal and report diagnostic |
| Captive remove Wi-Fi | local-connectivity mutation | Update Wi-Fi derived cache, remain in AP | Existing redirect/list refresh | Report diagnostic; do not claim removal verified |
| Connected Dial add/remove Wi-Fi | local-connectivity mutation | Update Wi-Fi derived cache; preserve current no-forced-reconnect behavior | Existing redirect | Report diagnostic; no stale cache write-back |
| Manual bridge set/clear | endpoint mutation, advances generation | Apply endpoint to bridge actor after config lock | Preserve target's current response/reboot behavior | No normal success/reboot; stale discovery response cannot overwrite |
| mDNS discovery | conditional endpoint mutation using token | Apply discovered endpoint to bridge actor | Continue current poll | Stale token is ignored, not reported as storage failure |
| Zone selection | zone mutation | Update bridge selected-zone/label state | Existing immediate selection/presentation | Keep prior zone and report failure |
| Remote preferences | conditional typed merge using request token | Apply copied preferences after config lock | Existing display/power refresh | Reject malformed/stale; committed-unverified is applied with diagnostic, never called verified |
| Factory reset | unchanged recovery effect outside config owner | Stop services, erase current device NVS, reboot | Current behavior | Current error logging |

## Verification Contract

### Native tests

- Missing/corrupt config applies existing defaults and persists once.
- Existing valid V3 config is published without resetting fields.
- Same-device V1/V2/V3 backend upgrade results remain accepted.
- Adding/updating/removing/promoting Wi-Fi preserves bridge URL, source, zone,
  display, and power fields in the compatibility blob.
- Bridge endpoint/source replacement preserves Wi-Fi, zone, and preferences.
- Zone replacement preserves Wi-Fi, bridge, and preferences.
- Remote preference replacement preserves Wi-Fi, bridge source, and zone.
- Failed persistence returns failure and leaves the published snapshot
  unchanged when not committed.
- Commit-success/read-back-failure publishes the candidate, returns
  committed-unverified, and cannot be mistaken for verified success.
- Full-field mismatch in each persisted V3 field, including every remote
  preference, prevents `COMMITTED_VERIFIED` on both targets.
- Invalid Wi-Fi counts, unterminated strings, oversized endpoint/zone values,
  and null inputs fail or normalize according to the existing contract.
- Interleaved local-connectivity and remote-preference transactions cannot
  reproduce stale full-blob overwrite.
- Empty NVS with and without `CONFIG_RK_DEFAULT_SSID/PASS` preserves current
  first-boot behavior.
- Init tests cover existing durable load, verified default commit,
  committed-unverified default commit, and storage-unavailable volatile
  recovery, including snapshot publication, durability state, actor start
  eligibility, presentation diagnostic, and no automatic erase/reboot.
- Partial remote preferences preserve absent fields; malformed strings,
  invalid rotation, out-of-range timeouts, and mixed valid/invalid payloads
  reject all fields and cause no display/power apply.
- Manual endpoint set/clear invalidates an outstanding mDNS or remote-preference
  token; an unrelated Wi-Fi mutation does not.
- Raw V1, V2, and V3 fixture bytes load/repair exactly as before, and the
  written V3 byte layout remains 570 bytes with unchanged field order.

### Dependency and target tests

- `common/wifi_manager.c` has no target include.
- Each target provisioning adapter starts/stops its target portal exactly once.
- No portal, settings surface, target UI, OTA, Wi-Fi manager, app entry, or
  bridge client calls raw storage load/save/reset mutation APIs directly.
- The negative dependency fixture still proves common-to-target includes fail.
- Dial and Frame target configuration remains device-local; tests contain no
  cross-target restore/translation API.
- A failing provisioning adapter cannot emit `AP_STARTED`; a separate capped
  service backoff retries without an AP/STA loop; success still starts the
  portal before that event, and stop cancels retries idempotently.
- Wi-Fi and bridge apply methods cannot persist or call back into a config
  mutation while holding actor locks.

### Hardware smoke

Run on the exact immutable PR artifacts for both targets:

- retained same-device settings after upgrade and reboot;
- fresh provisioning;
- add a second network, remove either network, and verify priority;
- wrong-password/no-AP retry followed by setup AP;
- AP setup followed by successful STA reconnect;
- manual bridge override and clear-to-mDNS behavior;
- zone selection and persisted reboot behavior;
- valid/partial server display/power preference refresh without losing Wi-Fi;
- Wi-Fi loss/reconnect and bridge recovery presentation;
- BLE media remote still pairs/operates according to #191;
- no boot loop, task overflow, unexpected reset, or lost existing settings.

Frame qualification remains gated by #188's exact-artifact hardware evidence.

## Boundaries and Non-goals

- No cross-device NVS migration, import, restore, or shared device state.
- No NVS namespace/key/version/layout change.
- No adaptive manifest/action payload.
- No HQPlayer or Home Assistant behavior.
- No new accessory driver, input binding persistence, or resource registry.
- No Frame sleep/power-policy redesign.
- No Wi-Fi/bridge recovery reducer or retry-threshold change.
- No portal redesign or shared HTML renderer.
- No factory reset semantic change.
- No BLE enablement/bond ownership; BLE remains in its existing separate NVS
  component.
- No merge or release without explicit maintainer approval.

## Accepted Trade-offs

- `rk_cfg_t` remains the internal compatibility DTO for one more slice. The
  ownership boundary removes unsafe mutation first; domain-specific persisted
  records can be considered later only with demonstrated value.
- Some read-only target consumers may continue taking copied full snapshots.
  The enforcement gate targets mutation/persistence, not a disruptive all-at-
  once type rewrite.
- Dial and Frame keep separate portal implementations. This preserves target UX
  while removing the dependency inversion that matters.
- Connectivity/recovery consolidation is deferred so this slice can be tested
  without changing user-visible retry behavior.

## Initial Solution Review

**Aim:** Establish one same-device configuration/persistence owner and remove
shared Wi-Fi's target portal dependency without changing the stored schema or
normal recovery behavior.

**Status:** ADJUST

### Alignment Check

- Necessary: Yes — competing complete-blob writers and common-to-target portal
  includes are current defects.
- Aligned: Yes — the selected owner/port boundaries directly address #190.
- Sufficient: Not initially — persistence outcomes, remote preference trust,
  runtime propagation, and owner bypasses were underspecified.
- Mechanism clear: Yes — field-scoped serialized transactions prevent unrelated
  writes from clobbering each other.
- Changes complete: No in the first draft — a half-migrated internal step still
  had two writers and was incorrectly described as releasable.

### Drift Detected

- Scope drift risk: calling B1–B3 independently releasable would permit a
  production state with both the new owner and legacy Wi-Fi writer.
- Solution drift risk: a vague remote-preference replacement could grow the
  owner into an untyped policy service.

### Decision

Keep the selected architecture but require an atomic production cutover. Define
typed/presence-aware remote preferences and validation, remove load/reset
persistence bypasses, and specify operation-by-operation runtime effects.

### Corrections Applied

- Replaced the pseudo-releasable B1/B2/B3 sequence with one atomic cutover.
- Added remote preference allowlist, presence, bounds, rejection, and no-apply
  semantics.
- Confined load migration/repair persistence and reset helpers to owner init.
- Added the runtime effect matrix and no-persist actor apply methods.
- Extended enforcement beyond direct `platform_storage_save()` calls.

## Initial Solution Dissent

**Decision under review:** Dedicated `controller_config` owner plus
target-neutral provisioning port.

**Stakes:** Stored Wi-Fi/bridge settings, boot-to-provisioning recovery, and the
architecture boundary used by every future target/backend.

**Confidence before dissent:** MEDIUM

### Steel-Man Position

A device-local transaction owner fixes the actual stale-writer defect without
changing schema, while a two-operation provisioning port removes target
selection from shared Wi-Fi code. Typed operations keep integration, UI, BLE,
and recovery effects outside the owner.

### Contrary Evidence

1. Compiled Wi-Fi defaults currently live in `wifi_manager`, not storage
   defaults.
2. Storage can commit successfully and then fail read-back verification, so a
   Boolean cannot promise rollback.
3. Wi-Fi and bridge currently retain independent full snapshots; writer
   migration alone could leave stale runtime state.
4. mDNS and remote HTTP complete outside locks and can overwrite a newer manual
   endpoint decision.
5. Portal start returns `void`; AP readiness can be advertised despite HTTP
   startup failure.
6. BLE uses separate NVS and must not be absorbed into this owner.

### Pre-Mortem Scenarios

1. **First-boot regression:** compiled credentials are omitted and the device
   unexpectedly enters AP mode.
2. **Durability split:** commit succeeds, verification fails, and RAM knowingly
   retains a different value than the next boot may load.
3. **Stale actor cache:** Wi-Fi reconnect writes an old complete snapshot back.
4. **Late external response:** mDNS/server results overwrite a newer endpoint.
5. **False provisioning readiness:** setup AP exists but no portal is serving.
6. **God-object creep:** BLE, bindings, recovery, or adaptive payloads enter a
   generic mutable config API.

### Hidden Assumptions

| Assumption | Evidence | Risk if wrong | Test |
|---|---|---|---|
| Owner preserves full default ordering | Split current implementation | First boot regresses | Empty-NVS fixtures with/without compiled credentials |
| Boolean save implies rollback | Commit precedes verify | RAM/reboot split | Three-outcome fake backend |
| One mutex fences external results | mDNS/HTTP run outside lock | Manual config loss | Endpoint-token interleavings |
| Portal start cannot fail | Existing API is `void` | Unusable setup AP | Failing adapter test |
| `rk_cfg_t` owns BLE | It does not | Config god object | Dependency/field allowlist |
| Same schema ensures downgrade | Normalization writes V3 | Silent compatibility drift | Raw V1/V2/V3 fixtures |

### Decision

**Recommendation:** ADJUST

**Reasoning:** The direction remains correct, but transaction outcomes,
compiled defaults, actor propagation, external-response fencing, provisioning
failure, and compatibility gates must be explicit before implementation.

**Corrections applied:** all six are now part of the selected contract.

**Confidence after dissent:** HIGH, conditional on final re-review/re-dissent.

**Create ADR?** Yes, after the final solution gates accept the corrected
contract.

## Corrected-Contract Review

**Status:** ADJUST

**Closed from the first review:** atomic cutover, typed/presence-aware remote
preferences, three persistence outcomes, owner-private storage mutation,
operation/effect matrix, actor propagation and lock ordering, compiled defaults,
endpoint tokens, and fallible provisioning startup.

**Remaining P2:** first-boot committed-unverified and storage-unavailable
behavior was contradictory. The general rule published a committed candidate,
while the matrix implied no valid snapshot, leaving Wi-Fi/AP start eligibility
undefined.

**Correction applied:** initialization now always publishes a valid copied
snapshot with `DURABLE`, `DEGRADED_COMMIT`, or `VOLATILE_RECOVERY` status before
actors start. Degraded/volatile states allow recovery startup, surface explicit
diagnostics, and never claim durability or automatically erase/reboot.

## Corrected-Contract Dissent

**Recommendation:** ADJUST

### Remaining Contrary Evidence

1. Existing Dial verification checks only SSID and Frame checks only selected
   fields, so "verified" did not prove remote preference durability.
2. No durable boot snapshot still needs volatile defaults to reach AP recovery.
3. Reusing STA credential retry for portal startup failure could create an
   AP/STA loop.
4. Owner-private storage was ambiguous while target OTA/UI still read the
   backend directly.
5. Endpoint-token coverage needed to include every fallback and rediscovery
   writer, not only primary mDNS/manual paths.

### Corrections Applied

- `COMMITTED_VERIFIED` now requires common full-field normalized V3 equality on
  both targets.
- Initialization has an explicit volatile recovery snapshot/durability state.
- Portal startup uses a separate capped service retry timer while AP remains
  stable; it never emits readiness early.
- Every production reader migrates to owner copies/projections; raw access is
  limited to owner/backends plus the explicit whole-NVS factory-reset effect.
- Every endpoint writer and remote-response path must use endpoint generation
  tokens.

**Confidence:** HIGH, subject to final exact-contract gates.

## Decision

Current decision: **proceed with C**.

## Final Exact-Contract Review

**Contract SHA-256:**
`cd513c496582e2d7d5591334a63f415b86e3fff606462b65a74d25e497c5f45f`

**Status:** CONTINUE

### Findings

- P0: none
- P1: none
- P2: none
- P3: none

### Assessment

Initialization now defines and tests `DURABLE`, `DEGRADED_COMMIT`, and
`VOLATILE_RECOVERY`. Each publishes a copied snapshot before bridge/Wi-Fi
startup, preserves compiled-credential versus AP behavior, emits the appropriate
presentation-path diagnostic, and forbids boot loops or automatic erasure.

The atomic cutover, typed remote preferences, commit outcomes, raw-storage
boundary, post-lock actor propagation, default ordering, endpoint fencing,
fallible portal lifecycle, operation/effect matrix, and no-migration constraint
are complete and internally consistent. `controller_config` remains a bounded
transaction owner rather than a runtime-effects god object.

## Final Exact-Contract Dissent

**Contract SHA-256:**
`cd513c496582e2d7d5591334a63f415b86e3fff606462b65a74d25e497c5f45f`

**Recommendation:** PROCEED

### Failure Modes Re-tested

- Full-field verification: resolved by common normalized equality for every
  persisted field.
- Committed-unverified/unavailable boot: resolved by degraded and volatile
  snapshots that preserve STA/AP recovery without false durability.
- Portal recovery: resolved by AP-stable capped service retry with no early
  `AP_STARTED`.
- Raw production reads: resolved by owner snapshots/projections and explicit
  backend/factory-reset boundaries.
- Endpoint races: resolved by tokens on manual, clear, fallback, periodic,
  mDNS, and remote response paths.
- Atomicity/migration: resolved by a single cutover, unchanged schema/key/layout,
  and no cross-device transfer surface.

### Decision

No architectural blocker remains. Implementation must return to solution-space
if it requires a schema/key/layout change, cross-device migration, generic
mutable config API, or a new bridge/UI dependency.

**Confidence after dissent:** HIGH

**ADR:** Recommended after maintainer acceptance; not required to begin the
reversible implementation.

## Execute

**Updated:** 2026-07-31
**Status:** in progress

### Pre-flight

- Aim is clear: establish one authoritative same-device configuration
  transaction boundary and remove shared Wi-Fi's target portal dependency.
- Constraints are known: unchanged NVS namespace/key/V3 layout and same-device
  upgrades; no cross-device migration; no bridge/UI/BLE/recovery god object; no
  altered normal retry/presentation behavior.
- Context is loaded: every current raw reader/writer, boot ordering, remote
  preference parser, mDNS endpoint writer, portal lifecycle, target storage
  backend, and dependency-policy edge is inventoried above.
- Scope is bounded: the atomic configuration/provisioning cutover only. Adaptive
  controls, HQPlayer, Home Assistant, accessories, input persistence, Frame
  power policy, and recovery reducer remain out of scope.
- Success is explicit: all production reads/writes use owner
  snapshots/projections, full-field verification and three durability outcomes
  pass native tests, stale endpoint responses fail closed, portal readiness is
  truthful, dependency enforcement passes, both PERF targets build, and exact
  artifacts pass hardware smoke before merge.

### Workstreams

1. Shared config owner, storage outcomes, compatibility/default fixtures.
2. Target-neutral provisioning port and AP-stable service retry.
3. Production reader/writer migration, actor apply methods, endpoint fencing,
   and remote preference validation.
4. Enforcement, native/sanitizer suites, target builds, and exact-artifact
   review/dissent/hardware evidence.

No intermediate commit is a releasable cutover until all four workstreams are
integrated.

Implementation must stop and return to solution-space if preserving current
behavior requires a schema change, cross-device migration, a generic mutable
config callback, or a new bridge/UI dependency.

## Execute Evidence

**Updated:** 2026-07-31

**Draft PR:** #222

**Decision:** accepted for commit/push; not accepted for merge or release

### Integrated result

- One shared `controller_config` owner now serializes the current device's
  existing V3 compatibility blob and exposes copied snapshots plus typed
  mutations. There is no cross-device NVS import, restore, or transfer path.
- Target backends retain the existing device-local namespace/key and decode the
  same V1/V2/V3 layouts. Read-side repair no longer writes; the owner decides
  persistence and preserves a readable recovered candidate in RAM when repair
  persistence fails.
- Writes distinguish `NOT_COMMITTED`, `COMMITTED_VERIFIED`, and
  `COMMITTED_UNVERIFIED`; runtime presentation distinguishes durable, degraded,
  and volatile recovery snapshots.
- Shared Wi-Fi uses a target-neutral provisioning port. ESP Wi-Fi effects and
  target provisioning effects have separate serialized gates; all three
  FreeRTOS mutexes use guarded static initialization.
- Dial has explicit port-80 ownership across config-server to captive-portal
  handoff. Frame replaces STA HTTP with AP HTTP under one lifecycle lock.
- Dial and Frame DNS tasks capture their own socket and cannot be restarted
  until a stopped task acknowledges retirement.
- Successful setup responds before a single deferred reboot/countdown task;
  the HTTP worker is no longer blocked for the reboot delay.
- Bridge endpoint and sparse remote-preference writes use typed owner methods
  and endpoint-generation fencing; local Wi-Fi, endpoint, zone, and display
  settings cannot overwrite one another through stale full-blob copies.

### Verification

- Full shared CI command block passes locally, including owner/default tests,
  sanitizers, provisioning adapters, Wi-Fi lifecycle races, controller seams,
  dependency/identity policy, and BLE HID host contracts.
- Forced Wi-Fi concurrency harness passes 30 consecutive runs and covers
  copied-credential interleaving, STA/AP effect ordering, and provisioning
  start/stop exclusion.
- Dependency policy: 260 allowed, 4 boundary-owned, 22 grandfathered; negative
  fixture passes.
- Clean Docker ESP-IDF v5.5.5 PERF builds after the final corrections:
  - Dial: `hiphi_dial.bin` 0x1bc210, 31% app partition free;
  - Frame: `hiphi_frame.bin` 0x107800, 74% app partition free.
- Both configs prove PERF optimization, absence of DEBUG optimization, 4096-byte
  ESP system event stack, and BLE/NimBLE HID inclusion. Frame also proves QIO
  at 80 MHz.
- `git diff --check` passes.

### Execute review and dissent

The first fresh execute review/dissent returned **ADJUST** for three P1 issues:

1. ESP lazy mutex initialization could create distinct locks under first-use
   contention;
2. Frame DNS could reuse globals before a delayed task acknowledged exit;
3. a decoded same-device config was replaced with defaults when its repair
   write was not committed.

All three were corrected with guarded static mutexes, task-local DNS sockets
plus stop/ack/reap fencing, and volatile publication of the recovered candidate
plus a regression test.

The post-correction Terra `/review` verdict is **ACCEPT**. The post-correction
Terra `/dissent` verdict is **ACCEPT for pushing the draft implementation**.
Neither is a merge or release verdict.

### Remaining gates

1. Push the implementation and require green CI on its exact commit SHA.
2. Download the exact CI Dial and Frame merged artifacts.
3. Physically validate Dial boot, existing-device config, AP provisioning,
   normal control, and BLE/Wi-Fi coexistence.
4. Physically validate Frame boot, AP provisioning/DNS, e-ink UI, input,
   media-remote pairing/control, and Wi-Fi/BLE coexistence.
5. Re-run review/dissent on exact-artifact evidence before marking #222 or the
   stacked program ready to merge/release.

## Exact-artifact rejection and bridge-worker correction

**Updated:** 2026-07-31

The replacement artifact from workflow run `30628409974` is **rejected**. On a
physical HiPhi Dial it successfully discovered Unified Hi-Fi Control through
the legacy compatibility mDNS service at `http://NAS2:8088`, then overflowed
the generic 8192-byte polling task while persisting the discovered endpoint.
The artifact rebooted with ELF SHA `8262b6eac`; discovery naming was therefore
not the failure.

Static optimized frame evidence places roughly 6640 bytes in the application
call chain from polling through endpoint mutation/commit before ESP-IDF NVS and
logging callees. The immediate correction:

- gives the polling worker the explicit `bridge_poll` name and a 16384-byte
  internal stack;
- uses an atomic single-owner start, allowing at most two starts per boot and
  retrying only on the first network false-to-true transition;
- exposes final allocation failure in the UI instead of claiming zones load;
- logs internal free heap/largest block before and after task creation and
  after endpoint persistence;
- logs the worker's lifetime stack high-water free bytes around persistence;
- retains the legacy compatibility mDNS identifier, endpoint paths, NVS fields,
  and protocol behavior;
- updates user-facing setup/status copy to Unified Hi-Fi Control (shortened to
  Hi-Fi Control only where device display space is constrained).

Native owner/input contracts, dependency/identity policy, and corrected Docker
ESP-IDF v5.5.5 PERF builds pass locally. Corrected sizes are Dial `0x1bc500`
(31% partition free) and Frame `0x107ab0` (74% free). The next pushed preview is
for exact-artifact hardware evidence, not merge authorization.
