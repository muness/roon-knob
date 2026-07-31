#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROLLER_MEDIA_TEXT_CAPACITY 128
#define CONTROLLER_ARTWORK_REF_CAPACITY 128
#define CONTROLLER_CONNECTIVITY_TEXT_CAPACITY 96

/*
 * Owned media patch. All strings are always NUL-terminated. The value may be
 * queued by ownership transfer and consumed synchronously on the UI task.
 */
typedef struct {
    char primary[CONTROLLER_MEDIA_TEXT_CAPACITY];
    char secondary[CONTROLLER_MEDIA_TEXT_CAPACITY];
    char tertiary[CONTROLLER_MEDIA_TEXT_CAPACITY];
    char artwork_ref[CONTROLLER_ARTWORK_REF_CAPACITY];
    bool playing;
    float volume;
    float volume_min;
    float volume_max;
    float volume_step;
    int32_t progress_position;
    int32_t progress_duration;
    uint32_t artwork_generation;
} controller_media_view_t;

/*
 * Recovery/connectivity is a separate patch so posting it does not allocate a
 * media-sized value or imply that it replaces durable media state.
 */
typedef struct {
    char headline[CONTROLLER_CONNECTIVITY_TEXT_CAPACITY];
    char detail[CONTROLLER_CONNECTIVITY_TEXT_CAPACITY];
} controller_connectivity_view_t;

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
    uint32_t artwork_generation);

void controller_connectivity_view_init(
    controller_connectivity_view_t *view,
    const char *headline,
    const char *detail);

#if defined(__cplusplus)
static_assert(sizeof(controller_media_view_t) <= 576,
              "controller media patch exceeds its ESP32 budget");
static_assert(sizeof(controller_connectivity_view_t) <= 192,
              "controller connectivity patch exceeds its ESP32 budget");
#else
_Static_assert(sizeof(controller_media_view_t) <= 576,
               "controller media patch exceeds its ESP32 budget");
_Static_assert(sizeof(controller_connectivity_view_t) <= 192,
               "controller connectivity patch exceeds its ESP32 budget");
#endif

#ifdef __cplusplus
}
#endif
