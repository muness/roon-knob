# Firmware CI reuse

Shared tests gate all firmware jobs. A failed shared test prevents any firmware build job from starting.

PR jobs fingerprint committed firmware inputs before modifying version files. The fingerprint includes target/variant, app code and configuration, shared code, component and toolchain locks, build scripts and workflow definitions. Current cross-app dependencies are included: RLCD uses Frame, and Atom/M5 use Tough. Ordinary host tests, web files and documentation do not invalidate firmware artifacts. Unknown paths are included conservatively. A shared component change may invalidate more boards than strictly necessary.

Only an exact GitHub cache key can reuse an artifact; there are no prefix fallbacks. Cache scope follows GitHub's PR isolation. Every file digest, the exact inventory and input fingerprint are checked before skipping build steps. A missing, expired or invalid cache entry rebuilds normally. Artifacts retain the original build SHA and run ID in per-target provenance JSON and the job summary, even when uploaded again by a newer run. The first run populates the cache. Release tags always build fresh and never save/reuse this cache.

This caches final verified files, not an ESP-IDF build directory. Existing compiler caches are used only when a real build is needed. Cache capacity/eviction may reduce reuse without affecting correctness. Dynamic upstream changes behind unpinned references cannot be inferred from git input hashes; update locks/workflow references to request changed toolchain inputs.

Tests exercise fingerprint invalidation, unaffected board/test/site changes, variant separation, digest/inventory failure, and original provenance preservation. Workflow contract tests check shared-test dependencies and the restore/build/upload structure. Actual cache hit rates remain observable in CI rather than promised.
