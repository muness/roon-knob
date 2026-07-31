# HiPhi Dial Agent Guide

## Open Horizons Framework

**The shift:** Action is cheap. Knowing what to do is scarce.

**The sequence:** aim → problem-space → problem-statement → solution-space → execute → ship

Use the installed skill that matches the altitude of the work:

- Cannot explain the intended behavior change → `/aim`
- Repeated blockers or accumulating patches → `/problem-space`
- Solutions feel forced or the framing may be wrong → `/problem-statement`
- About to choose an implementation → `/solution-space`
- Ready to implement a clear approach → `/execute`
- Code is complete and needs to reach a testable user artifact → `/ship`

Reflection skills are available at every phase:

- `/review` checks alignment and detects drift.
- `/dissent` actively seeks contrary evidence before consequential decisions.
- `/salvage` preserves learning before restarting or reintroducing prior work.
- `/distill` curates reusable metis and guardrails across sessions.

Enter at the altitude the work needs. Move back up when evidence invalidates the
current framing.

## Delivery Workflow

GitHub Issues is the source of truth for tasks. Do not maintain parallel task
databases or Markdown TODO lists.

1. Run `gh issue list` and inspect dependencies before starting.
2. Claim or create the correct issue; use task lists on program/epic issues.
3. For significant program work, follow `solution-space → execute → ship`.
4. Run `/review` and `/dissent` at each phase boundary. Post both reports to the
   PR with the issue number, phase, exact commit SHA, decision, evidence, and any
   remaining blockers so the output contract is auditable.
5. Keep PRs draft until their required hardware artifact is available and tested.
6. Never merge a PR or create/push a release tag without explicit user approval.

## Project Context

### Purpose

HiPhi Dial turns commodity embedded hardware into approachable, dedicated hi-fi
controllers. The product should let someone perform routine listening without
opening a general-purpose screen: see what is playing and control transport,
volume, zones, playlists, and programs.

### Current Aims

- Factor a shared controller core for playback state, commands, configuration,
  connectivity, and recovery, cleanly decoupled from displays and physical input.
- Drive adaptive UI from server-provided control payloads. Full HQPlayer zone
  support is the first end-to-end implementation; Home Assistant follows the same
  capability model rather than a parallel UI architecture.
- Support Dial/round, HiPhi Frame/e-ink, M5 Tough, AtomS3 + Joystick, and future
  encoder/joystick/button modules through explicit target and capability profiles.
- Make BLE HID media-remote pairing a shared capability on compatible targets,
  preserving the existing Frame behavior.
- End the current program with testable beta firmware for both HiPhi Dial and
  HiPhi Frame.
- Explore playlist/program selection, including voice control, after the shared
  control and adaptive-UI foundations are reliable.

### Key Constraints

- Preserve the proven Frame/e-ink and BLE HID behavior from historical mainline
  commit `46599a6`. `origin/v4` is a salvage/reference source and the
  known-working Dial build profile, not the provenance of those features and not
  a wholesale merge target.
- Hardware claims must be tied to an exact target/revision. Do not infer flash,
  PSRAM, display, touch, power, or pin capabilities from a product-family name.
- Device-specific UI and input drivers may be compile-time inclusions; playback,
  connectivity, configuration, and recovery semantics belong in shared code.
- A green compile or configuration gate is not a working firmware artifact.
  Shipping requires the exact CI artifact to pass flash, sustained boot, and
  relevant display/input/connectivity checks on physical hardware.
- CI release builds must use an optimized, evidence-backed configuration. The
  recovery baseline carries v4's non-debug build settings onto master (PERF
  optimization and a 16 MB merged image). The v2.5.2 Dial artifact establishes
  ESP-IDF 5.5.5 as hardware-proven for Dial; each additional target, including
  Frame, still requires exact-artifact hardware evidence.
- This is a side project: prefer incremental factoring with shippable slices over
  an unbounded rewrite.

### Patterns to Follow

- Put target differences behind narrow platform/display/input interfaces.
- Represent target capabilities explicitly and fail safely on unknown hardware.
- Salvage behavior and learning from prior branches; direct code reuse is optional.
- Record decisions and hardware evidence in the relevant GitHub issue/PR and ADR.
- Keep documentation aligned with the effective build and the artifact users test.

### Anti-Patterns to Avoid

- Cloning the application per board.
- Coupling playback/source behavior directly to LVGL widgets or a display shape.
- Treating all ESP32-family boards as interchangeable.
- Calling an artifact fixed before an exact-SHA physical test.
- Replacing a known-working full build profile with isolated guessed settings.

### Decision Context

The maintainer approves architecture, merge, and release decisions. “Done” means
the scoped issue is implemented, review/dissent output contracts are posted,
automated checks pass, and any required exact-artifact hardware validation is
recorded. Program completion additionally requires beta artifacts for Dial and
Frame.
