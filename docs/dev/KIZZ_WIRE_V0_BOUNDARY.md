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
