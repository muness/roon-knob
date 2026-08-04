#include "bridge_command_plan.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static bridge_command_context_t context(bool operational) {
    bridge_command_context_t value = {
        .operational = operational,
        .zone_id = "zone-living",
        .volume = -20.0f,
        .volume_min = -80.0f,
        .volume_max = 6.0f,
        .volume_step = 0.5f,
    };
    return value;
}

static void test_simple_commands(void) {
    bridge_command_context_t ctx = context(false);
    bridge_command_plan_t plan;

    controller_command_t command =
        controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.accepted);
    assert(!plan.no_op);
    assert(!plan.updates_volume);
    assert(plan.failure_feedback ==
           BRIDGE_COMMAND_FEEDBACK_PLAYBACK_FAILED);
    assert(strcmp(plan.json,
                  "{\"zone_id\":\"zone-living\","
                  "\"action\":\"play_pause\"}") == 0);

    command = controller_command_make(CONTROLLER_COMMAND_NEXT_TRACK);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.accepted);
    assert(plan.failure_feedback == BRIDGE_COMMAND_FEEDBACK_NEXT_FAILED);
    assert(strcmp(plan.json,
                  "{\"zone_id\":\"zone-living\",\"action\":\"next\"}") == 0);

    command = controller_command_make(CONTROLLER_COMMAND_PREVIOUS_TRACK);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.accepted);
    assert(plan.failure_feedback ==
           BRIDGE_COMMAND_FEEDBACK_PREVIOUS_FAILED);
    assert(strcmp(plan.json,
                  "{\"zone_id\":\"zone-living\",\"action\":\"prev\"}") == 0);
}

static void test_volume_readiness_noop_and_clamp(void) {
    bridge_command_context_t ctx = context(false);
    bridge_command_plan_t plan;
    controller_command_t command = controller_command_adjust_volume(5);

    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(!plan.accepted);
    assert(!plan.updates_volume);
    assert(plan.rejection_feedback == BRIDGE_COMMAND_FEEDBACK_CONNECTING);
    assert(plan.json[0] == '\0');

    command = controller_command_adjust_volume(0);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.accepted);
    assert(plan.no_op);
    assert(!plan.updates_volume);
    assert(plan.json[0] == '\0');

    ctx.operational = true;
    command = controller_command_adjust_volume(3);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.accepted);
    assert(plan.updates_volume);
    assert(plan.predicted_volume == -18.5f);
    assert(plan.volume_step == 0.5f);
    assert(plan.failure_feedback == BRIDGE_COMMAND_FEEDBACK_VOLUME_FAILED);
    assert(strcmp(plan.json,
                  "{\"zone_id\":\"zone-living\",\"action\":\"vol_abs\","
                  "\"value\":-18.5}") == 0);

    ctx.volume_step = 0.25f;
    command = controller_command_adjust_volume(1);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.predicted_volume == -19.75f);
    assert(strcmp(plan.json,
                  "{\"zone_id\":\"zone-living\",\"action\":\"vol_abs\","
                  "\"value\":-19.75}") == 0);

    ctx.volume_step = 0.5f;
    command = controller_command_adjust_volume(INT_MAX);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.predicted_volume == 6.0f);
    assert(strstr(plan.json, "\"value\":6}") != NULL);

    command = controller_command_adjust_volume(INT_MIN);
    assert(bridge_command_plan_build(&command, &ctx, &plan));
    assert(plan.predicted_volume == -80.0f);
    assert(strstr(plan.json, "\"value\":-80}") != NULL);
}

static void test_fail_closed_and_bounded_output(void) {
    bridge_command_context_t ctx = context(true);
    bridge_command_plan_t plan;
    controller_command_t none =
        controller_command_make(CONTROLLER_COMMAND_NONE);
    assert(!bridge_command_plan_build(&none, &ctx, &plan));
    assert(!plan.accepted);

    controller_command_t unknown =
        controller_command_make((controller_command_kind_t)999);
    assert(!bridge_command_plan_build(&unknown, &ctx, &plan));
    assert(!plan.accepted);

    assert(!bridge_command_plan_build(NULL, &ctx, &plan));
    assert(!bridge_command_plan_build(&none, NULL, &plan));
    assert(!bridge_command_plan_build(&none, &ctx, NULL));

    ctx.zone_id = NULL;
    controller_command_t toggle =
        controller_command_make(CONTROLLER_COMMAND_TOGGLE_PLAYBACK);
    assert(bridge_command_plan_build(&toggle, &ctx, &plan));
    assert(strcmp(plan.json,
                  "{\"zone_id\":\"\",\"action\":\"play_pause\"}") == 0);
    assert(strlen(plan.json) < BRIDGE_COMMAND_JSON_CAPACITY);
}

int main(void) {
    test_simple_commands();
    test_volume_readiness_noop_and_clamp();
    test_fail_closed_and_bounded_output();
    puts("bridge command planning contracts passed");
    return 0;
}
