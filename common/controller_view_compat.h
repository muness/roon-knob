#pragma once

#include "controller_view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Translate owned controller patches to the existing link-time-selected
 * presentation API. Call on the existing UI task; the target adapter consumes
 * the supplied value synchronously.
 */
void controller_view_compat_apply_media(const controller_media_view_t *view);
void controller_view_compat_apply_connectivity(
    const controller_connectivity_view_t *view);

/* Reset only compatibility bookkeeping; no renderer call is made. */
void controller_view_compat_reset(void);

#ifdef __cplusplus
}
#endif
