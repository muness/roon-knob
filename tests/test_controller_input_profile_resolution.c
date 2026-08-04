#include "controller_input.h"
#include "platform/platform_task.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    (void)fn;
    (void)arg;
    return false;
}

#if defined(TEST_DIAL_PROFILE)

static void test_actual_dial_profile_resolution(void) {
    controller_physical_event_t event = {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .value = -2,
        .sequence = 1,
        .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
    };
    controller_action_t action = controller_action_none();

    assert(controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    assert(action.kind == CONTROLLER_ACTION_COMMAND);
    assert(action.value.command.kind == CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS);
    assert(action.value.command.volume_steps == -3);

    event.value = 9;
    assert(controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER, &action));
    assert(action.kind == CONTROLLER_ACTION_SCROLL_ZONE_PICKER);
    assert(action.value.picker_delta == 1);

    event.gesture = CONTROLLER_PHYSICAL_GESTURE_REPEAT;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
}

#elif defined(TEST_FRAME_PROFILE)

static void test_actual_frame_profile_resolution(void) {
    controller_physical_event_t event = {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
        .kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD,
        .value = 1,
        .sequence = 1,
        .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
    };
    controller_action_t action = controller_action_none();

    assert(controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    assert(action.kind == CONTROLLER_ACTION_SYSTEM);
    assert(action.value.system ==
           CONTROLLER_SYSTEM_ACTION_START_PROVISIONING);

    event.control_id = CONTROLLER_INPUT_CONTROL_FRAME_PWR;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));

    event.control_id = CONTROLLER_INPUT_CONTROL_FRAME_BOOT;
    event.value = 2;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));

    event.value = 1;
    event.flags = CONTROLLER_PHYSICAL_EVENT_FLAG_INITIAL;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
}

#else
#error "Compile with TEST_DIAL_PROFILE or TEST_FRAME_PROFILE"
#endif

int main(void) {
#if defined(TEST_DIAL_PROFILE)
    test_actual_dial_profile_resolution();
#else
    test_actual_frame_profile_resolution();
#endif
    puts("controller input target-profile resolver contracts passed");
    return 0;
}
