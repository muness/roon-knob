#pragma once

#include "controller_input.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Immutable controls and default bindings selected by the firmware target.
 * The returned storage has static lifetime.
 */
const controller_input_descriptor_t *controller_input_profile_descriptors(
    size_t *out_count);
const controller_input_binding_t *controller_input_profile_bindings(
    size_t *out_count);

#ifdef __cplusplus
}
#endif
