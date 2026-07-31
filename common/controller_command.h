#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTROLLER_COMMAND_NONE = 0,
    CONTROLLER_COMMAND_TOGGLE_PLAYBACK,
    CONTROLLER_COMMAND_NEXT_TRACK,
    CONTROLLER_COMMAND_PREVIOUS_TRACK,
    CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS,
} controller_command_kind_t;

typedef struct {
    controller_command_kind_t kind;
    int32_t volume_steps;
} controller_command_t;

static inline controller_command_t controller_command_make(
    controller_command_kind_t kind) {
    controller_command_t command = {
        .kind = kind,
        .volume_steps = 0,
    };
    return command;
}

static inline controller_command_t controller_command_adjust_volume(
    int32_t signed_steps) {
    controller_command_t command = {
        .kind = CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS,
        .volume_steps = signed_steps,
    };
    return command;
}

#if defined(__cplusplus)
static_assert(sizeof(controller_command_t) <= 8,
              "controller command exceeds its ESP32 budget");
#else
_Static_assert(sizeof(controller_command_t) <= 8,
               "controller command exceeds its ESP32 budget");
#endif

#ifdef __cplusplus
}
#endif
