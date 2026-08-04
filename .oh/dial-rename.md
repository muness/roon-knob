# HiPhi Dial Identity Migration

**Issue:** #162
**Program:** #201
**Depends on:** #216, #217
**Coordinates with:** #163, #164, #174, #211 / PR #212

## Problem Statement

Owners and maintainers need the round controller to identify itself consistently
as **HiPhi Dial**, without breaking firmware discovery, OTA consumers, hosted
flashers, or historical evidence that still truthfully refers to the former
Roon Knob name.

The key constraint is that product identity, protocol identity, artifact
identity, repository identity, and source-directory layout do not have the same
migration boundary.

Success means current user-facing firmware, current documentation, build
metadata, and primary release assets say HiPhi Dial; compatibility-sensitive
legacy names remain only as explicit, tested transition aliases or truthful
historical references owned by separate follow-up issues.

## Solution Space

**Updated:** 2026-07-30

### Candidates Considered

| Option | Level | Approach | Principal trade-off |
| --- | --- | --- | --- |
| A | Band-Aid | Change visible UI strings only | Leaves artifacts, hostnames, logs, and docs inconsistent |
| B | Local optimum | Global textual rename, including paths and URLs | Breaks OTA/discovery and rewrites historical truth |
| C | Reframe | Migrate each identity according to its compatibility boundary | Requires temporary aliases and explicit exceptions |
| D | Redesign | Introduce a general multi-product identity registry before renaming | Useful later, but excessive for two compile-time targets |

### Evaluation

#### Option A — visible strings

- Solves the stated problem: partially.
- Implementation cost: low.
- Maintenance burden: high because two active names remain unexplained.
- Second-order effect: users download or flash `roon_knob` artifacts for a
  device whose screen says HiPhi Dial.

#### Option B — global rename

- Solves the stated problem: superficially.
- Implementation cost: high and conflict-prone.
- Maintenance burden: hidden in downstream breakage.
- Second-order effect: old OTA consumers cannot find firmware; changing
  `_roonknob._tcp` before dual-listen support breaks discovery; new repository
  and hosted URLs would not exist yet; historical analysis becomes inaccurate.

#### Option C — compatibility-bounded migration

- Solves the stated problem: yes, incrementally.
- Implementation cost: medium.
- Maintenance burden: bounded aliases with named retirement owners.
- Second-order effect: release assets temporarily contain both primary and
  compatibility filenames, but their relationship is testable and explicit.
- Optionality: #163 can migrate discovery independently, #164 can move the
  repository/domain after redirects exist, and #174 can move source paths after
  the behavioral stack lands.

#### Option D — general identity registry

- Solves the stated problem: yes.
- Implementation cost: high.
- Maintenance burden: low only after more runtime-selectable products exist.
- Second-order effect: turns a compile-time product rename into a platform
  redesign before the target-profile work in #193 has defined the real axes.

### Recommendation

**Selected:** Option C — compatibility-bounded migration
**Level:** Reframe

Migrate the current Dial product identity now while preserving only the legacy
identifiers that have active compatibility or provenance value.

#### Change in this issue

- User-visible round-target name: `Roon Knob` → `HiPhi Dial`.
- Default round-target slug/hostname: `roon-knob` → `hiphi-dial`.
- Provisioning SSID: `roon-knob-setup` → `hiphi-dial-setup`.
- Dial project/application and primary release artifacts:
  `roon_knob` → `hiphi_dial`.
- Active current-product documentation and web-flasher labels.
- Target-owned log/TXT identity.
- Regression checks that distinguish forbidden active identity from permitted
  compatibility/provenance references.

#### Preserve deliberately

- `_roonknob._tcp` until #163 ships dual-query/dual-advertise compatibility.
- `muness/roon-knob` and `roon-knob.muness.com` until #164 performs and verifies
  the administrative migration.
- `roon_knob.bin` and `roon_knob_merged.bin` as byte-identical release aliases
  for older OTA/flasher consumers; #164 or an explicit release-policy issue owns
  retirement after at least two compatible releases.
- Historical release notes, analysis, citations, and old command/output examples
  when changing the name would falsify provenance.
- Persistent schema fields such as `knob_name`; renaming stored keys or moving
  NVS between devices is unnecessary and out of scope.

#### Keep separate

- #174 moves `idf_app/` and `frame_app/`; no user-visible outcome depends on
  combining the directory move with this rename.
- #211 / PR #212 owns the attributable hardware record and identity guard.
  Rebase after it lands, update its truthful current-product terminology, and
  retain its provenance categories.

### Accepted Trade-offs

- Release assets temporarily expose both new primary and old compatibility
  filenames.
- Repository and hosted-flasher URLs retain the old slug for one transition
  period even while page titles say HiPhi Dial.
- The source tree keeps `idf_app/` until #174.

### Implementation and Verification Contract

1. Add target identity characterization checks before changing strings.
2. Rename runtime and build identity without changing stored user configuration.
3. Generate new primary artifacts plus byte-for-byte legacy aliases.
4. Update active docs; classify remaining old-name occurrences as compatibility
   or historical provenance, never accidental leftovers.
5. Build Dial and Frame with ESP-IDF 5.5.5 and run release-config fixtures.
6. Verify a fresh Dial shows `hiphi-dial-setup`, while an upgraded configured
   Dial retains Wi-Fi/zone/BLE settings.
7. Verify web flashing and OTA lookup using the new primary asset and the legacy
   alias before removing either.

## Dissent

**Updated:** 2026-07-30
**Decision:** ADJUST

The compatibility-boundary approach is correct, but the first draft treated
artifact aliases and OTA as if they were one surface. They are not.

### Adjustments accepted

- Produce both application and merged-image names at every artifact surface:
  Actions artifacts, GitHub Releases, hosted Pages, and PR previews. The
  `hiphi_dial` files are primary; `roon_knob` files are generated only by
  byte-for-byte copy and checked with `cmp`.
- Point the current web manifest and current UI at the primary filename while
  keeping stable legacy binary URLs live.
- Treat bridge OTA selection as a cross-repository contract owned by
  `open-horizon-labs/unified-hifi-control#277`; this issue proves firmware,
  release, and web-flasher artifacts, not bridge selection behavior.
- Keep `_roonknob._tcp` exclusively under #163. A source audit across the local
  Open Horizon Labs checkouts found no consumer of Dial's `_device-info`
  `product` TXT field, so this issue may change that value to `hiphi-dial`
  without changing the service type.
- Verify four persistence/recovery cases: fresh, configured, custom-named, and
  AP-mode/no-credential Dial. Do not change `rk_cfg`, its `cfg` NVS key,
  `rk_cfg_t`, `cfg_ver`, or `knob_name`.
- Maintain a checked exception inventory whose categories are compatibility,
  deferred external endpoints, and historical provenance. Each entry names its
  owner and retirement or retention condition.
- Keep `idf_app/` and `frame_app/` mechanically unchanged; #174 alone owns the
  directory move.

### Dissent result

**ADJUST, then proceed.** The rename remains an identity migration rather than
a global substitution. The strongest risk—one release surface still requesting
an old file that no longer exists—is addressed only when the artifact matrix is
generated and checked end to end.

## Execute

**Updated:** 2026-07-30
**Status:** implementation complete; hardware validation remains external

- [x] Aim: current round-controller identity is HiPhi Dial everywhere an owner
  encounters it.
- [x] Hard constraints: preserve protocol/repository/domain compatibility;
  preserve NVS schema and values; do not move source directories.
- [x] Context: runtime identity seam, firmware build, release workflow, Pages,
  PR previews, manifests, docs, and bridge OTA ownership have been inspected.
- [x] Scope: #162 product/build identity plus compatibility aliases; #163,
  #164, #174, and UHC #277 remain independently owned.
- [x] Success evidence: strict identity/exception fixtures, native tests, clean
  ESP-IDF 5.5.5 builds for Dial and Frame, artifact equality, manifest lookup,
  and exact-artifact hardware checks after CI.

### Implementation evidence

- Runtime identity: CMake application `hiphi_dial`, product slug
  `hiphi-dial`, provisioning SSID `hiphi-dial-setup`, matching log/UI/mDNS TXT
  copy, and unchanged `_roonknob._tcp`.
- Persistence: no changes to `rk_cfg`, `cfg`, `rk_cfg_t`, `cfg_ver`, or
  `knob_name`; the checked identity contract requires these invariants.
- Artifact matrix: Actions, Release, PR preview, and Pages publish primary
  `hiphi_dial` application/merged files plus byte-identical `roon_knob`
  aliases. Current manifests select the primary merged file.
- Native contracts: configuration, BLE report mapping, BLE lifecycle policy,
  and BLE-disabled stubs pass.
- ESP-IDF 5.5.5 clean builds:
  - HiPhi Dial: ESP32-S3, PERF/O3, BLE host enabled, `hiphi_dial.bin`
    `0x1b8010` bytes.
  - HiPhi Frame: ESP32-S3, PERF/O3, BLE host enabled, QIO configuration,
    `hiphi_frame.bin` `0x103710` bytes.
  - BLE-disabled fixture: ESP32-S3 build contains only the stub and no NimBLE
    HID host objects.
- Generated Dial application aliases and merged aliases passed `cmp`; the web
  manifest parsed and resolved to the generated `hiphi_dial_merged.bin`.
- Workflow YAML parses and passes actionlint's workflow checks. Existing
  unrelated shellcheck findings remain outside this slice.

### Hardware evidence still required

- Fresh/no-credential Dial advertises `hiphi-dial-setup` with matching screen
  and captive-portal copy.
- Configured Dial retains Wi-Fi, zone, BLE, bridge, and custom `knob_name`.
- Exact CI artifacts flash and boot on Dial; the Frame and BLE stack retain
  their separate hardware gates from #216 and #217.

## Review

**Updated:** 2026-07-30
**Aim:** Rename the round controller to HiPhi Dial without breaking active
artifact, discovery, hosted-flasher, repository, or stored-configuration
contracts.
**Status:** ADJUST, then CONTINUE

### Alignment check

- Necessary: yes; product copy, runtime identity, artifacts, and documentation
  otherwise exposed contradictory names.
- Aligned: yes after corrections; the slice does not move source directories,
  rename the repository/domain, change mDNS service type, or migrate stored
  configuration.
- Sufficient: yes; the new checker is narrow to identity and compatibility
  policy rather than a general product registry.
- Mechanism clear: yes; new names are primary while explicit byte-identical
  aliases preserve consumers until their named owners migrate.
- Changes complete: complete for code and local verification; hardware and
  exact CI artifact checks are explicitly pending.

### Drift detected and corrected

1. Active docs initially renamed the external Roon extension to a product name
   it does not expose. They now use its verified `Unified Hi-Fi Control` name.
2. The first pass missed two Kconfig menu labels. Both now use target-neutral or
   HiPhi Dial copy, and the checker recognizes additional case variants.
3. The repository-layout example initially implied the repository had already
   moved. It now preserves the current `roon-knob/` slug as a checked #164
   exception.
4. A removed PC simulator's historical executable was initially rewritten. Its
   command capture now remains truthful and explicitly historical.

### Decision

Continue to an exact candidate commit, then obtain independent review and
dissent before opening the stacked draft PR. Do not claim hardware completion
or release readiness from build evidence alone.

## Independent Review and Dissent

**Reviewed candidate:** `ede0615702f669fd577d3ae90698314bf0d3d68a`

The execute-stage review returned **ADJUST** and the execute-stage dissent
returned **BLOCK**. Both independently found that the implementation promised
application and merged-image pairs at every surface, but PR previews and Pages
published only the merged pair.

### Findings accepted and corrected

- PR previews now publish immutable primary/legacy application files and
  primary/legacy merged files, download all four from the deployed preview, and
  byte-compare both pairs.
- Pages now publishes stable primary/legacy application and merged files and
  byte-compares both pairs.
- Release preparation now byte-compares both pairs after staging them.
- Preview deployment now depends on `test-shared`, so a failed identity contract
  cannot publish a preview.
- The checked contract now requires the complete application/merged matrix at
  both preview and Pages surfaces.
- README and both active flashing guides now name the actual “Flash HiPhi Dial”
  button.
- A local generated-output simulation used the real ESP-IDF 5.5.5 Dial build
  and reproduced the Release, immutable-preview, and Pages layouts; both pairs
  had identical SHA-256 values at every layout.

### Deliberately unresolved gates

- `open-horizon-labs/unified-hifi-control#277` still owns testing and migrating
  bridge selection from `roon_knob.bin` to `hiphi_dial.bin`. This firmware
  change preserves the legacy alias but does not claim the primary name works
  through bridge OTA yet.
- Exact CI artifacts still require Dial hardware validation for fresh/AP-mode,
  configured, and custom-name paths. #216 and #217 retain their Frame and BLE
  hardware gates.

The corrected commit requires a fresh independent review and dissent before the
draft PR is opened.
