# HiPhi Frame recovery — issue #188

## Session

- Phase: execute → hardware beta handoff
- Baseline: `origin/master` at `9f25fa5` (v2.5.2)
- Branch: `codex/issue-188-frame-recovery-v2`
- Draft PR: #216
- Historical Frame behavior source: mainline commit `46599a6`
- Agent policy: Terra review/dissent; prior Sonnet contracts are superseded by
  explicit maintainer direction and intentionally removed.

## Aim

Restore the previously working HiPhi Frame experience on the current release
line, backed by the current shared controller state, commands, configuration,
connectivity, and recovery behavior. The recovery must preserve Dial behavior
and create a clean second target that can drive later factoring.

## Scope boundary

- Frame display, PMIC, raw buttons, and current BLE host implementation remain
  target-owned.
- Shared controller code owns playback state/commands and normalizes input and
  presentation.
- Dial and Frame use the same v3 configuration schema and shared Wi-Fi manager,
  but each device reads and writes only its own local NVS.
- Same-device Dial/Frame v1/v2 blobs upgrade locally to v3 and persist once.
- There is no Dial↔Frame import, restore, transfer, reconciliation, or sync.
- Connected saved-network edits are non-disruptive and take effect at the next
  restart. Captive recovery promotes the submitted network immediately.
- Adaptive UI, shared BLE extraction (#191), Dial rename, and power policy
  (#160) remain subsequent program slices.

## Implemented contract

1. `controller_presentation` is target-neutral; shared controller code contains
   no e-ink/LVGL selection conditional.
2. Physical and BLE media inputs use normalized `controller_input` actions.
3. Worker, Wi-Fi, captive-portal, and BLE presentation changes are marshalled to
   the UI queue; e-ink state and rendering remain single-threaded.
4. Frame BOOT long-press queues an explicit shared Wi-Fi provisioning request.
5. Frame JSON and artwork HTTP requests send `X-Device-Type: frame`.
6. Wi-Fi v3:
   - maximum two entries;
   - corrupt counts/strings normalize before helpers use them;
   - a third entry is rejected until the user explicitly removes one;
   - captive setup promotes the submitted network;
   - field-level connectivity persistence cannot overwrite bridge/zone/display
     configuration;
   - both captive portals expose recovery removal and surface save failures.
7. Frame BLE media callbacks never perform bridge HTTP synchronously.
8. PR previews and tagged releases build and package both target artifacts.
9. Frame uses runtime QIO/80 MHz, but its merged boot image retains the
   ESP-IDF-required DIO header.

## Automated evidence

- Host `rk_cfg` contract tests: pass.
- Workflow YAML and both web manifests: parse.
- `git diff --check`: pass.
- Dial ESP-IDF 5.5.5 PERF build: pass.
  - app size: `0x1b4d30`
  - partition headroom: 32%
- Frame ESP-IDF 5.5.5 PERF build: pass.
  - app size: `0x140000`
  - partition headroom: 68%
  - NimBLE/HID gates: enabled
  - system event task stack: 4096
  - runtime flash: QIO at 80 MHz
- Frame merged image:
  - `esptool image_info`: valid checksum and validation hash
  - boot header mode byte: `2` (DIO)
- Exact local merged artifact SHA-256:
  - Dial: `3bd3dc11116db93c0d738227497de7932b824cf1ab5103293990923553066ae0`
  - Frame: `c94880d6d25950c4ed4ebc17a68b2b7985a1ddd54cdecdd465781d9b489d01c1`

## Review and dissent

- Earlier Terra gates found and caused fixes for stale full-config writes,
  corrupt-count bounds, silent list eviction, migration durability, network
  promotion, Frame identity, release packaging, BLE callback affinity,
  provisioning recovery, UI-thread ownership, and persistence error reporting.
- Final current-tree Terra review: **PASS — no remaining code blockers**.
- Final current-tree Terra dissent: **PROCEED — no remaining P0/P1/P2 code
  blocker**.
- Both gates keep exact-artifact hardware characterization as the remaining
  beta/release evidence.

## Hardware beta contract

Use only the exact PR-preview artifact produced after the final commit:

1. Flash HiPhi Frame through PR #216’s Frame button and retain the CI artifact
   SHA/commit SHA in the test note.
2. Capture serial from power-on through provisioning, bridge discovery, zone
   selection, artwork, and at least 30 minutes of sustained operation.
3. Cold boot repeatedly from USB and battery; verify no reset loop.
4. From both connected and failed-network states, hold BOOT and verify
   `hiphi-frame-setup` appears and accepts credentials.
5. Exercise two-network failover and explicit removal/replacement.
6. Verify now-playing text/artwork, zone selection persistence, transport, and
   volume behavior.
7. Pair, reconnect, use, and unpair a BLE HID media remote while Wi-Fi and
   artwork refresh are active.
8. Record PMIC/battery behavior. Public beta remains gated by #160’s measured
   sleep/wake policy; a successful bring-up artifact is not yet a public release.

## Next transition

After code review/dissent and CI are green, push the exact PR artifact for
maintainer hardware testing. Hardware evidence determines whether #188 can move
to the public-beta gate or needs a targeted recovery fix. Shared BLE extraction
for Dial remains #191 and starts only after Frame behavior is characterized.
