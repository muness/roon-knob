#pragma once

/*
 * Generic snapshot/action boundary for wire-v0 snapshot version 1.
 *
 * The snapshot reuses the canonical surface_projection_t bounded structs.
 * Sections contain ordered controls, which are the wire-v0 projected items.
 * The boundary carries generic labels, content, values, options, ranges,
 * control kinds, emphasis, projected affect, and opaque IDs/action refs.
 *
 * Wire-v0 snapshot version 1 has no media/resource member. Media-dependent
 * composition remains gated on Agent Surface issue #292. This API therefore
 * exposes no resource URL, asset key, artwork field, or equivalent escape
 * hatch.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "surface_client_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A wire-v0 control is the generic ordered item consumed by a renderer. */
typedef surface_control_t surface_projection_item_t;

typedef struct {
    bool has_numeric_value;
    double numeric_value;
    bool has_text_value;
    char text_value[SURFACE_ID_LEN];
} surface_projection_input_value_t;

typedef struct {
    uint32_t projection_revision;
    char control_id[SURFACE_ID_LEN];
    char action_ref[SURFACE_ACTIONREF_LEN];
    surface_projection_input_value_t value;
} surface_projection_input_t;

typedef bool (*surface_projection_input_sink_t)(
    const surface_projection_input_t *input,
    void *context);

typedef struct {
    surface_projection_t projection;
    uint32_t revision;
    bool has_projection;
    bool connection_ready;
    bool session_authorized;
    bool projection_current;
    surface_projection_input_sink_t input_sink;
    void *input_context;
} surface_projection_snapshot_t;

void surface_projection_snapshot_init(
    surface_projection_snapshot_t *snapshot,
    surface_projection_input_sink_t input_sink,
    void *input_context);

/* These transitions can only close the action gate. */
void surface_projection_snapshot_set_connection_ready(
    surface_projection_snapshot_t *snapshot,
    bool ready);
void surface_projection_snapshot_authorize_session(
    surface_projection_snapshot_t *snapshot);
void surface_projection_snapshot_deauthorize_session(
    surface_projection_snapshot_t *snapshot);

/* Publish one already-admitted bounded wire-v0 projection. */
bool surface_projection_snapshot_publish(
    surface_projection_snapshot_t *snapshot,
    const surface_projection_t *projection);

/* Mark the current revision stale until a fresh projection is published. */
void surface_projection_snapshot_mark_stale(
    surface_projection_snapshot_t *snapshot);

bool surface_projection_snapshot_copy(
    const surface_projection_snapshot_t *snapshot,
    surface_projection_t *out);

bool surface_projection_snapshot_can_emit(
    const surface_projection_snapshot_t *snapshot);

/* Emit only opaque IDs/action refs plus optional numeric/text value. */
bool surface_projection_snapshot_emit(
    surface_projection_snapshot_t *snapshot,
    const surface_projection_input_t *input);

#ifdef __cplusplus
}
#endif
