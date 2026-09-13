#!/bin/sh
set -eu

cc_bin=${CC:-cc}
probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/surface-projection-bounds.XXXXXX")
trap 'rm -rf "$probe_dir"' EXIT HUP INT TERM

cat > "$probe_dir/bounds_probe.c" <<'EOF'
#include "surface_client_protocol.h"
#include "surface_projection_snapshot.h"

#ifdef EXPECT_SMALLER_TARGET
_Static_assert(sizeof(surface_projection_t) < 66264,
               "smaller target projection did not shrink");
_Static_assert(sizeof(surface_projection_snapshot_t) < 66288,
               "smaller target snapshot did not shrink");
#else
_Static_assert(sizeof(surface_projection_t) == 66264,
               "default projection size drift");
_Static_assert(sizeof(surface_projection_snapshot_t) == 66288,
               "default snapshot size drift");
#endif
int main(void) { return 0; }
EOF

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic \
    -Icommon "$probe_dir/bounds_probe.c" -o "$probe_dir/default"
"$probe_dir/default"

"$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic \
    -DSURFACE_MAX_SECTIONS=2 -DSURFACE_MAX_CONTROLS=3 \
    -DSURFACE_MAX_OPTIONS=2 -DEXPECT_SMALLER_TARGET -Icommon \
    "$probe_dir/bounds_probe.c" -o "$probe_dir/smaller"

for definition in \
    SURFACE_MAX_SECTIONS=0 SURFACE_MAX_CONTROLS=0 SURFACE_MAX_OPTIONS=0 \
    SURFACE_MAX_SECTIONS=7 SURFACE_MAX_CONTROLS=9 SURFACE_MAX_OPTIONS=7; do
    if "$cc_bin" -std=c11 -Wall -Wextra -Werror -pedantic \
        -D"$definition" -Icommon \
        "$probe_dir/bounds_probe.c" -o "$probe_dir/invalid" \
        >/dev/null 2>&1; then
        echo "invalid target bound unexpectedly compiled: $definition" >&2
        exit 1
    fi
done

echo "surface projection default, smaller override, and invalid-bound compile checks passed"
