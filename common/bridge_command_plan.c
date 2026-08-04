#include "bridge_command_plan.h"

#include <stdio.h>
#include <string.h>

static bool build_simple_plan(bridge_command_plan_t *plan,
                              const char *zone_id,
                              const char *action,
                              bridge_command_feedback_t failure_feedback) {
    int written = snprintf(
        plan->json, sizeof(plan->json),
        "{\"zone_id\":\"%s\",\"action\":\"%s\"}",
        zone_id, action);
    if (written < 0 || (size_t)written >= sizeof(plan->json)) {
        return false;
    }
    plan->accepted = true;
    plan->failure_feedback = failure_feedback;
    return true;
}

bool bridge_command_plan_build(const controller_command_t *command,
                               const bridge_command_context_t *context,
                               bridge_command_plan_t *plan) {
    if (!command || !context || !plan) {
        return false;
    }

    memset(plan, 0, sizeof(*plan));
    const char *zone_id = context->zone_id ? context->zone_id : "";

    switch (command->kind) {
    case CONTROLLER_COMMAND_TOGGLE_PLAYBACK:
        return build_simple_plan(
            plan, zone_id, "play_pause",
            BRIDGE_COMMAND_FEEDBACK_PLAYBACK_FAILED);
    case CONTROLLER_COMMAND_NEXT_TRACK:
        return build_simple_plan(
            plan, zone_id, "next", BRIDGE_COMMAND_FEEDBACK_NEXT_FAILED);
    case CONTROLLER_COMMAND_PREVIOUS_TRACK:
        return build_simple_plan(
            plan, zone_id, "prev",
            BRIDGE_COMMAND_FEEDBACK_PREVIOUS_FAILED);
    case CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS:
        if (command->volume_steps == 0) {
            plan->accepted = true;
            plan->no_op = true;
            return true;
        }
        if (!context->operational) {
            plan->rejection_feedback = BRIDGE_COMMAND_FEEDBACK_CONNECTING;
            return true;
        }

        plan->predicted_volume =
            context->volume +
            ((float)command->volume_steps * context->volume_step);
        if (plan->predicted_volume < context->volume_min) {
            plan->predicted_volume = context->volume_min;
        }
        if (plan->predicted_volume > context->volume_max) {
            plan->predicted_volume = context->volume_max;
        }

        int written = snprintf(
            plan->json, sizeof(plan->json),
            "{\"zone_id\":\"%s\",\"action\":\"vol_abs\","
            "\"value\":%.10g}",
            zone_id, plan->predicted_volume);
        if (written < 0 || (size_t)written >= sizeof(plan->json)) {
            memset(plan, 0, sizeof(*plan));
            return false;
        }
        plan->accepted = true;
        plan->updates_volume = true;
        plan->volume_step = context->volume_step;
        plan->failure_feedback = BRIDGE_COMMAND_FEEDBACK_VOLUME_FAILED;
        return true;
    case CONTROLLER_COMMAND_NONE:
    default:
        return false;
    }
}
