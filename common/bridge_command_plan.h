#pragma once

#include "controller_command.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRIDGE_COMMAND_JSON_CAPACITY 256

typedef enum {
    BRIDGE_COMMAND_FEEDBACK_NONE = 0,
    BRIDGE_COMMAND_FEEDBACK_CONNECTING,
    BRIDGE_COMMAND_FEEDBACK_PLAYBACK_FAILED,
    BRIDGE_COMMAND_FEEDBACK_NEXT_FAILED,
    BRIDGE_COMMAND_FEEDBACK_PREVIOUS_FAILED,
    BRIDGE_COMMAND_FEEDBACK_VOLUME_FAILED,
} bridge_command_feedback_t;

typedef struct {
    bool operational;
    const char *zone_id;
    float volume;
    float volume_min;
    float volume_max;
    float volume_step;
} bridge_command_context_t;

typedef struct {
    bool accepted;
    bool no_op;
    bool updates_volume;
    float predicted_volume;
    float volume_step;
    bridge_command_feedback_t rejection_feedback;
    bridge_command_feedback_t failure_feedback;
    char json[BRIDGE_COMMAND_JSON_CAPACITY];
} bridge_command_plan_t;

/*
 * Build the synchronous bridge compatibility action for a semantic command.
 * No transport, renderer, allocation, queue, or global state is touched.
 */
bool bridge_command_plan_build(const controller_command_t *command,
                               const bridge_command_context_t *context,
                               bridge_command_plan_t *plan);

#ifdef __cplusplus
}
#endif
