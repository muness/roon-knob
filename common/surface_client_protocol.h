#pragma once

/*
 * Wire-model-compatible bounded data model for Agent Surface wire-v0 snapshot
 * version 1. The canonical contract is the shared_surface_runtime model at
 * immutable upstream commit 0e455fc1 (Agent Surface PR #291).
 *
 * This local copy is deliberately data-only: parsing, authority,
 * rendering, and physical input remain outside this header. Its field names,
 * enum values, and bounds are checked against the immutable conformance
 * manifest in docs/dev/surface_wire_v0_conformance_manifest.json.
 *
 * Wire-v0 snapshot version 1 has no media or resource field. That omission is
 * intentional and is governed by Agent Surface issue #292; callers must not
 * infer or smuggle a visual resource through content, labels, or IDs.
 */

#include <stdbool.h>
#include <stdint.h>

#include "surface_projected_affect.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SURFACE_WIRE_V0_SNAPSHOT_VERSION 1u
#define SURFACE_WIRE_V0_REFERENCE_COMMIT "0e455fc1"

#define SURFACE_WIRE_V0_SCHEMA_MAX_SECTIONS 6u
#define SURFACE_WIRE_V0_SCHEMA_MAX_CONTROLS 8u
#define SURFACE_WIRE_V0_SCHEMA_MAX_OPTIONS 6u

#ifndef SURFACE_MAX_SECTIONS
#define SURFACE_MAX_SECTIONS 6
#endif
#ifndef SURFACE_MAX_CONTROLS
#define SURFACE_MAX_CONTROLS 8
#endif
#ifndef SURFACE_MAX_OPTIONS
#define SURFACE_MAX_OPTIONS 6
#endif

#ifndef SURFACE_ID_LEN
#define SURFACE_ID_LEN 32
#endif
#ifndef SURFACE_LABEL_LEN
#define SURFACE_LABEL_LEN 48
#endif
#ifndef SURFACE_CONTENT_LEN
#define SURFACE_CONTENT_LEN 96
#endif
#ifndef SURFACE_FALLBACK_LEN
#define SURFACE_FALLBACK_LEN 160
#endif
#ifndef SURFACE_ACTIONREF_LEN
#define SURFACE_ACTIONREF_LEN 64
#endif
#ifndef SURFACE_ICON_LEN
#define SURFACE_ICON_LEN 12
#endif

typedef enum {
    SURFACE_CONTROL_MOMENTARY = 0,
    SURFACE_CONTROL_TOGGLE,
    SURFACE_CONTROL_SELECT_ONE,
    SURFACE_CONTROL_SELECT_CONTINUOUS,
    SURFACE_CONTROL_STATUS_LINE,
    SURFACE_CONTROL_DETAIL_REQUEST,
    SURFACE_CONTROL_VOICE_INTENT,
} surface_control_kind_t;

typedef struct {
    char option_id[SURFACE_ID_LEN];
    char label[SURFACE_LABEL_LEN];
    char action_ref[SURFACE_ACTIONREF_LEN];
} surface_option_t;

typedef struct {
    char control_id[SURFACE_ID_LEN];
    /* The wire field is "kind"; this is the existing portable controlKind
     * vocabulary represented by the canonical enum. */
    surface_control_kind_t kind;
    char label[SURFACE_LABEL_LEN];
    char content[SURFACE_CONTENT_LEN];
    char deterministic_fallback[SURFACE_FALLBACK_LEN];
    bool sensitive;
    int32_t emphasis;
    char action_ref[SURFACE_ACTIONREF_LEN];
    uint8_t option_count;
    surface_option_t options[SURFACE_MAX_OPTIONS];
    bool has_range;
    double range_min;
    double range_max;
    double range_step;
    bool has_value;
    double value;
    char value_text[SURFACE_ID_LEN];
    bool has_text_value;
    char icon[SURFACE_ICON_LEN];
} surface_control_t;

typedef struct {
    char section_id[SURFACE_ID_LEN];
    char label[SURFACE_LABEL_LEN];
    uint8_t control_count;
    surface_control_t controls[SURFACE_MAX_CONTROLS];
} surface_section_t;

typedef struct {
    char projection_id[SURFACE_ID_LEN];
    bool origin_llm;
    bool has_affect;
    surface_projected_affect_t affect;
    uint8_t section_count;
    surface_section_t sections[SURFACE_MAX_SECTIONS];
} surface_projection_t;

#ifdef __cplusplus
#define SURFACE_WIRE_STATIC_ASSERT static_assert
#else
#define SURFACE_WIRE_STATIC_ASSERT _Static_assert
#endif
SURFACE_WIRE_STATIC_ASSERT(SURFACE_MAX_SECTIONS ==
                               SURFACE_WIRE_V0_SCHEMA_MAX_SECTIONS,
                           "wire-v0 section bound drift");
SURFACE_WIRE_STATIC_ASSERT(SURFACE_MAX_CONTROLS ==
                               SURFACE_WIRE_V0_SCHEMA_MAX_CONTROLS,
                           "wire-v0 control bound drift");
SURFACE_WIRE_STATIC_ASSERT(SURFACE_MAX_OPTIONS ==
                               SURFACE_WIRE_V0_SCHEMA_MAX_OPTIONS,
                           "wire-v0 option bound drift");
#undef SURFACE_WIRE_STATIC_ASSERT

#ifdef __cplusplus
}
#endif
