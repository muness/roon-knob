// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef HIPHI_ARTWORK_VIEWPORT_H
#define HIPHI_ARTWORK_VIEWPORT_H

#include <stdbool.h>

typedef enum {
    ARTWORK_FIT,
    ARTWORK_COVER,
    ARTWORK_CIRCLE,
} artwork_scale_t;

typedef struct {
    int width;
    int height;
    artwork_scale_t scale;
} artwork_viewport_t;

static inline bool artwork_viewport_equal(artwork_viewport_t a, artwork_viewport_t b) {
    return a.width == b.width && a.height == b.height && a.scale == b.scale;
}

#endif
