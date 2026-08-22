# Generic wire-v0 snapshot/action boundary

Issue #237 adds a target-local adapter around the bounded Agent Surface
wire-v0 snapshot model. The local definitions are wire-model-compatible with
`firmware/shared_surface_runtime/surface_client_protocol.h` at immutable
upstream commit `0e455fc1` (Agent Surface PR #291); they are not a mutable
sibling checkout dependency.

The boundary exposes ordered sections and controls/items with generic labels,
content, values, options, ranges, control kinds, emphasis, projected affect,
opaque IDs, and opaque action references. It validates the current snapshot
before accepting an input and emits only the opaque control/action references
plus an optional numeric or text value through a callback.

Input emission requires all four conditions: an accepted snapshot, the current
projection revision, connection readiness, and an authorized session. Marking a
revision stale closes the input gate until a new snapshot is published. The
callback has no fixed action enum and does not grant authority.

## Deliberate wire-v0 gap

Wire-v0 snapshot version 1 has no media/resource field. The generic API and its
vectors therefore carry no resource URL, asset key, artwork descriptor, or
equivalent field. Media-dependent composition remains gated on the planned
Agent Surface generic resource issue [#292](https://github.com/open-horizon-labs/agent-surface/issues/292).

That future issue must define an optional host-authorized, bounded,
domain-neutral visual resource descriptor, lifecycle and authority semantics,
and embedded parser compatibility before any media-dependent template can be
claimed by the downstream composition work.

The focused vectors use only opaque IDs and generic schema values. They cover
all seven wire control kinds, options, ranges, projected affect, two opaque
projection vectors, lifecycle gating, stale revisions, exact action matching,
and bounded rejection.

Issue #237 establishes this prerequisite boundary only. Renderer consumption,
composition plans, drawing, and touch realization remain owed by #236; #237
does not claim those characteristics.

The manifest checker parses the local C enum order, model field inventory,
bounds macros, and projected-affect fields against the immutable manifest. It
also checks that no media/resource escape field has entered the model and runs
an in-memory mutation negative test.

The legacy layout-specific generator is intentionally not a #237 verification
artifact. The #233/#236 correction must replace that gate with evidence over
the real generic snapshot/action boundary.

## Bounds and the #236 integration guardrail

The manifest separates three different values:

- **Schema maxima** are the immutable wire-v0 compatibility limits: 6 sections,
  8 controls per section, and 6 options per control.
- **Default target bounds** retain those full values in the host and current
  firmware build.
- **Target-effective bounds** are the `SURFACE_MAX_*` macros after any
  compile-time target specialization. Each must remain positive and no greater
  than its schema maximum. The local header keeps the defaults behind `#ifndef`
  so a canonical target override remains effective; the manifest checker and
  compile tests verify both the fallback and override paths.

On the default host ABI, `sizeof(surface_projection_t)` is 66,264 bytes and
`sizeof(surface_projection_snapshot_t)` is 66,288 bytes. These are sizing facts,
not permission to put either object on a firmware task stack.

Issue #236 integration must treat that size as a hard storage boundary:

- Never allocate a full projection or snapshot as an automatic local on the
  12 KiB UI task stack.
- Use one owned static or bounded buffer for the target-effective model. Where
  the Kizz target needs external memory, qualify the allocation for Kizz PSRAM
  explicitly; do not assume a product-family name implies a memory capability.
- Do not retain duplicate full projection/snapshot copies during admission,
  rendering, or handoff. Any staging strategy must account for the effective
  bound and ownership before it is integrated.
- Measure the actual allocation and report target-effective section/control/
  option bounds, projection size, snapshot size, storage capability, and peak
  usage in target telemetry. A passing host compile is not allocation evidence.

This is an integration guardrail for #236, not renderer implementation in #237.
