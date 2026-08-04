#include "controller_view_compat.h"

#include "controller_presentation.h"

#include <stdint.h>
#include <string.h>

static char s_last_artwork_ref[CONTROLLER_ARTWORK_REF_CAPACITY];
static uint32_t s_last_artwork_generation;

void controller_view_compat_reset(void) {
    s_last_artwork_ref[0] = '\0';
    s_last_artwork_generation = 0;
}

static void apply_artwork(const controller_media_view_t *view) {
    bool reference_changed =
        strcmp(view->artwork_ref, s_last_artwork_ref) != 0;
    bool generation_changed =
        view->artwork_generation != s_last_artwork_generation;

    if (reference_changed) {
        controller_presentation_set_artwork(view->artwork_ref);
    } else if (view->artwork_ref[0] != '\0' && generation_changed) {
        /*
         * Existing target renderers deduplicate by key. Clearing first keeps
         * the established same-key forced refresh behavior without changing
         * either target adapter.
         */
        controller_presentation_set_artwork("");
        controller_presentation_set_artwork(view->artwork_ref);
    }

    memcpy(s_last_artwork_ref, view->artwork_ref,
           sizeof(s_last_artwork_ref));
    s_last_artwork_ref[sizeof(s_last_artwork_ref) - 1] = '\0';
    s_last_artwork_generation = view->artwork_generation;
}

void controller_view_compat_apply_media(const controller_media_view_t *view) {
    if (!view) {
        return;
    }

    controller_presentation_update(
        view->primary,
        view->secondary,
        view->tertiary,
        view->playing,
        view->volume,
        view->volume_min,
        view->volume_max,
        view->volume_step,
        (int)view->progress_position,
        (int)view->progress_duration);
    apply_artwork(view);
}

void controller_view_compat_apply_connectivity(
    const controller_connectivity_view_t *view) {
    if (!view) {
        return;
    }

    /*
     * Preserve the existing recovery presentation mapping. The connectivity
     * value itself deliberately carries no fabricated media fields.
     */
    controller_presentation_update(
        view->headline, view->detail, "",
        false, 0.0f, 0.0f, 100.0f, 1.0f, 0, 0);
}
