# Controller boundary characterization — issue #190

## Session

- Phase: execute
- Status: complete; awaiting independent review and dissent
- Baseline: `1e572377c209267bdd27f73f3371b697fabe3c17`
- Branch: `codex/issue-190-boundary-contract`
- Stack: #216 → #217 → #218 → this slice
- Agent policy: Terra review/dissent; no Claude/Sonnet or Superego tooling

## Aim

Make the first controller boundaries enforceable and behaviorally characterized
without treating the current transitional APIs as the finished adaptive
architecture.

## Pre-flight

- Aim is clear: prevent new coupling while preserving the working Dial and
  Frame paths.
- Constraints are known: no directory moves, no cross-device configuration
  transfer, no adaptive protocol, and no large `bridge_client` extraction.
- Context is loaded: #216 established presentation/input seams, #217 exercises
  queued BLE input, and #218 changes identity only.
- Scope is bounded: architecture decision, exact direct-include policy, negative
  policy fixture, and deterministic input/presentation characterization.
- Success is explicit: new or stale restricted edges fail; queue/input and both
  target presentation adapters have native contract tests; both embedded
  targets still build.

## Selected solution

Use a contract-first strangler:

1. Record the desired dependency DAG separately from the observed graph.
2. Label current APIs as retained, transitional, or target-owned.
3. Enforce only direct quoted includes in this slice. Do not claim component or
   link dependency isolation while both targets still build a single IDF
   component.
4. Inventory each permitted restricted edge exactly. Transitional exceptions
   require an owner and retirement condition; wildcard exceptions are invalid.
5. Characterize stable semantics rather than pixels, timing, or incidental call
   order.

## Execute

**Updated:** 2026-07-31
**Status:** complete

Implementation is intentionally stacked and must remain draft until the
underlying Dial/Frame hardware evidence and final rebases exist.
The final merge candidate must target `master` and pass the full workflow; the
current `pull_request` branch filter does not run it on intermediate stack PRs.

### Evidence

- Exact dependency policy passes with 190 allowed and 25 individually owned,
  grandfathered edges.
- Adversarial fixtures prove a comment-separated, line-spliced new target
  include, an ownerless forbidden edge, and `list(APPEND)`, inline `SRCS`,
  `target_sources`, `add_subdirectory`, and CMake `include` source mutations
  fail closed, as do a source synthesized through `set_property` and ESP-IDF
  `SRC_DIRS` mutation.
- Native input, Dial presentation, Frame presentation, configuration, and BLE
  HID contracts pass with warnings treated as errors.
- Clean ESP-IDF v5.5.5 ESP32-S3 builds pass for both targets.
- Both generated configurations select `CONFIG_COMPILER_OPTIMIZATION_PERF=y`
  and do not select `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y`.
- Dial binary: `0x1b8010` bytes; Frame binary: `0x103710` bytes.

## Explicit exclusions

- Closing #190 from this first slice.
- Moving `idf_app/`, `frame_app/`, or `common/`.
- Sharing or migrating NVS blobs between devices.
- Implementing versioned adaptive server payloads or command IDs.
- Canonizing picker queries or the current command-bound input enum.
- Treating native tests as Wi-Fi/BLE/e-ink hardware proof.
