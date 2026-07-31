#include "controller_view.h"

#include <stddef.h>

static void copy_bounded(char *destination, size_t capacity,
                         const char *source) {
    if (!destination || capacity == 0) {
        return;
    }

    if (!source) {
        source = "";
    }

    size_t length = 0;
    while (length + 1 < capacity && source[length] != '\0') {
        destination[length] = source[length];
        ++length;
    }
    destination[length] = '\0';
}

void controller_media_view_init(
    controller_media_view_t *view,
    const char *primary,
    const char *secondary,
    const char *tertiary,
    bool playing,
    float volume,
    float volume_min,
    float volume_max,
    float volume_step,
    int32_t progress_position,
    int32_t progress_duration,
    const char *artwork_ref,
    uint32_t artwork_generation) {
    if (!view) {
        return;
    }

    copy_bounded(view->primary, sizeof(view->primary), primary);
    copy_bounded(view->secondary, sizeof(view->secondary), secondary);
    copy_bounded(view->tertiary, sizeof(view->tertiary), tertiary);
    copy_bounded(view->artwork_ref, sizeof(view->artwork_ref), artwork_ref);
    view->playing = playing;
    view->volume = volume;
    view->volume_min = volume_min;
    view->volume_max = volume_max;
    view->volume_step = volume_step;
    view->progress_position = progress_position;
    view->progress_duration = progress_duration;
    view->artwork_generation = artwork_generation;
}

void controller_connectivity_view_init(
    controller_connectivity_view_t *view,
    const char *headline,
    const char *detail) {
    if (!view) {
        return;
    }

    copy_bounded(view->headline, sizeof(view->headline), headline);
    copy_bounded(view->detail, sizeof(view->detail), detail);
}
