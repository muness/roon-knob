#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The only public semantic presentation surface.  Native composition names,
 * geometry, and realization handles deliberately do not appear here. */
const char *controller_presentation_semantic_version(void);
bool controller_presentation_semantic_admit(
    const char *contract_json, size_t contract_len,
    char *evidence_json, size_t evidence_capacity);
bool controller_presentation_semantic_apply(const char *contract_id);

#ifdef __cplusplus
}
#endif
