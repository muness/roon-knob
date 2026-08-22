#pragma once

/*
 * Wire-model-compatible copy of the optional wire-v0 projected affect value.
 * This header intentionally contains no application, target, asset, action,
 * or event vocabulary.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double valence;
    double arousal;
    double intensity;
} surface_projected_affect_t;

#ifdef __cplusplus
}
#endif
