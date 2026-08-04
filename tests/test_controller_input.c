#include "controller_input.h"
#include "controller_input_profile.h"
#include "platform/platform_task.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define EXPECTED_QUEUE_CAPACITY 63
#define ACTION_CAPACITY (EXPECTED_QUEUE_CAPACITY + 16)

static controller_action_t s_actions[ACTION_CAPACITY];
static size_t s_action_count;

#define TEST_FRAME_BINDING(control, interaction_context, system_action)         \
    {                                                                           \
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,                     \
        .control_id = (control),                                                \
        .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,                         \
        .gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD,                            \
        .context = (interaction_context),                                       \
        .transform = CONTROLLER_INPUT_TRANSFORM_FIXED,                          \
        .flags = CONTROLLER_INPUT_BINDING_LOCKED_DEFAULT,                       \
        .action = {                                                             \
            .kind = CONTROLLER_ACTION_SYSTEM,                                   \
            .value.system = (system_action),                                    \
        },                                                                      \
    }

static const controller_input_binding_t s_test_bindings[] = {
    {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .context = CONTROLLER_INTERACTION_CONTEXT_MEDIA,
        .transform = CONTROLLER_INPUT_TRANSFORM_ROTATION_ACCELERATED,
        .action = {
            .kind = CONTROLLER_ACTION_COMMAND,
            .value.command = {
                .kind = CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS,
                .volume_steps = 1,
            },
        },
    },
    {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .context = CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
        .transform = CONTROLLER_INPUT_TRANSFORM_ROTATION_DIRECTION,
        .action = {
            .kind = CONTROLLER_ACTION_SCROLL_ZONE_PICKER,
            .value.picker_delta = 1,
        },
    },
    {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .context = CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
        .transform = CONTROLLER_INPUT_TRANSFORM_ROTATION_ACCELERATED,
        .action = {
            .kind = CONTROLLER_ACTION_COMMAND,
            .value.command = {
                .kind = CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS,
                .volume_steps = 1,
            },
        },
    },
    TEST_FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
                       CONTROLLER_INTERACTION_CONTEXT_MEDIA,
                       CONTROLLER_SYSTEM_ACTION_START_PROVISIONING),
    TEST_FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
                       CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                       CONTROLLER_SYSTEM_ACTION_START_PROVISIONING),
    TEST_FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
                       CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
                       CONTROLLER_SYSTEM_ACTION_START_PROVISIONING),
    TEST_FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_GP4,
                       CONTROLLER_INTERACTION_CONTEXT_MEDIA,
                       CONTROLLER_SYSTEM_ACTION_RESTART),
    TEST_FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_GP4,
                       CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                       CONTROLLER_SYSTEM_ACTION_RESTART),
    TEST_FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_GP4,
                       CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
                       CONTROLLER_SYSTEM_ACTION_RESTART),
};

static const controller_input_descriptor_t s_test_descriptors[] = {
    {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .flags = CONTROLLER_INPUT_DESCRIPTOR_REBINDABLE,
        .allowed_gestures = CONTROLLER_PHYSICAL_GESTURE_MASK(
            CONTROLLER_PHYSICAL_GESTURE_NONE),
        .allowed_event_flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
        .value_policy = CONTROLLER_INPUT_VALUE_NONZERO_SIGNED_DELTA,
        .label = "Dial encoder",
    },
    {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .flags = CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT,
        .allowed_gestures = CONTROLLER_PHYSICAL_GESTURE_MASK(
            CONTROLLER_PHYSICAL_GESTURE_HOLD),
        .allowed_event_flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
        .value_policy = CONTROLLER_INPUT_VALUE_UNIT_POSITIVE,
        .label = "Frame BOOT",
    },
    {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = CONTROLLER_INPUT_CONTROL_FRAME_GP4,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .flags = CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT,
        .allowed_gestures = CONTROLLER_PHYSICAL_GESTURE_MASK(
            CONTROLLER_PHYSICAL_GESTURE_HOLD),
        .allowed_event_flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
        .value_policy = CONTROLLER_INPUT_VALUE_UNIT_POSITIVE,
        .label = "Frame GP4",
    },
    {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = CONTROLLER_INPUT_CONTROL_FRAME_PWR,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .flags = CONTROLLER_INPUT_DESCRIPTOR_NONE,
        .allowed_gestures = CONTROLLER_PHYSICAL_GESTURE_MASK(
            CONTROLLER_PHYSICAL_GESTURE_HOLD),
        .allowed_event_flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
        .value_policy = CONTROLLER_INPUT_VALUE_UNIT_POSITIVE,
        .label = "Frame PWR (unused)",
    },
};

static const controller_input_binding_t *s_active_bindings = s_test_bindings;
static size_t s_active_binding_count =
    sizeof(s_test_bindings) / sizeof(s_test_bindings[0]);
static const controller_input_descriptor_t *s_active_descriptors =
    s_test_descriptors;
static size_t s_active_descriptor_count =
    sizeof(s_test_descriptors) / sizeof(s_test_descriptors[0]);

const controller_input_descriptor_t *controller_input_profile_descriptors(
    size_t *out_count) {
    if (out_count) {
        *out_count = s_active_descriptor_count;
    }
    return s_active_descriptors;
}

const controller_input_binding_t *controller_input_profile_bindings(
    size_t *out_count) {
    if (out_count) {
        *out_count = s_active_binding_count;
    }
    return s_active_bindings;
}

static bool record_action(const controller_action_t *action) {
    assert(action);
    assert(s_action_count < ACTION_CAPACITY);
    s_actions[s_action_count++] = *action;
    return true;
}

static void reset_profiles(void) {
    s_active_bindings = s_test_bindings;
    s_active_binding_count = sizeof(s_test_bindings) / sizeof(s_test_bindings[0]);
    s_active_descriptors = s_test_descriptors;
    s_active_descriptor_count =
        sizeof(s_test_descriptors) / sizeof(s_test_descriptors[0]);
}

static void reset_actions(void) {
    memset(s_actions, 0, sizeof(s_actions));
    s_action_count = 0;
    reset_profiles();
    controller_input_set_action_handler(record_action);
    assert(controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_MEDIA));
}

static void assert_command(const controller_action_t *action,
                           controller_command_kind_t kind,
                           int32_t steps) {
    assert(action);
    assert(action->kind == CONTROLLER_ACTION_COMMAND);
    assert(action->value.command.kind == kind);
    assert(action->value.command.volume_steps == steps);
}

static controller_physical_event_t dial_rotation(int32_t ticks) {
    controller_physical_event_t event = {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .value = ticks,
        .sequence = 7,
        .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
    };
    return event;
}

static controller_physical_event_t frame_long_press(uint16_t control_id) {
    controller_physical_event_t event = {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = control_id,
        .kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD,
        .value = 1,
        .sequence = 9,
        .flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
    };
    return event;
}

static void test_dial_rotation_resolution(void) {
    const int32_t ticks[] =
        {0, 1, -1, 2, -2, 3, -3, 99, -99, INT_MIN};
    const int32_t expected[] = {1, -1, 3, -3, 5, -5, 5, -5, -5};
    size_t expected_index = 0;

    for (size_t i = 0; i < sizeof(ticks) / sizeof(ticks[0]); ++i) {
        controller_action_t action = controller_action_none();
        controller_physical_event_t event = dial_rotation(ticks[i]);
        bool resolved = controller_input_resolve_physical(
            &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action);
        if (ticks[i] == 0) {
            assert(!resolved);
            continue;
        }
        assert(resolved);
        assert_command(&action, CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS,
                       expected[expected_index++]);
    }
    assert(expected_index == sizeof(expected) / sizeof(expected[0]));

    controller_action_t picker_action = controller_action_none();
    controller_physical_event_t positive = dial_rotation(99);
    assert(controller_input_resolve_physical(
        &positive, CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
        &picker_action));
    assert(picker_action.kind == CONTROLLER_ACTION_SCROLL_ZONE_PICKER);
    assert(picker_action.value.picker_delta == 1);

    controller_physical_event_t negative = dial_rotation(-99);
    assert(controller_input_resolve_physical(
        &negative, CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
        &picker_action));
    assert(picker_action.value.picker_delta == -1);

    controller_action_t settings_action = controller_action_none();
    assert(controller_input_resolve_physical(
        &positive, CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
        &settings_action));
    assert_command(&settings_action, CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS,
                   5);
}

static void test_frame_locked_defaults_and_unused_pwr(void) {
    controller_action_t action = controller_action_none();
    controller_physical_event_t boot =
        frame_long_press(CONTROLLER_INPUT_CONTROL_FRAME_BOOT);
    assert(controller_input_resolve_physical(
        &boot, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    assert(action.kind == CONTROLLER_ACTION_SYSTEM);
    assert(action.value.system ==
           CONTROLLER_SYSTEM_ACTION_START_PROVISIONING);

    controller_physical_event_t gp4 =
        frame_long_press(CONTROLLER_INPUT_CONTROL_FRAME_GP4);
    assert(controller_input_resolve_physical(
        &gp4, CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY, &action));
    assert(action.kind == CONTROLLER_ACTION_SYSTEM);
    assert(action.value.system == CONTROLLER_SYSTEM_ACTION_RESTART);

    controller_physical_event_t pwr =
        frame_long_press(CONTROLLER_INPUT_CONTROL_FRAME_PWR);
    assert(!controller_input_resolve_physical(
        &pwr, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
}

static void test_public_dispatch_rejects_system_actions(void) {
    reset_actions();

    controller_physical_event_t boot =
        frame_long_press(CONTROLLER_INPUT_CONTROL_FRAME_BOOT);
    assert(!controller_input_dispatch_physical(&boot));
    assert(s_action_count == 0);

    static const controller_input_binding_t unlocked_system_binding = {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD,
        .context = CONTROLLER_INTERACTION_CONTEXT_MEDIA,
        .transform = CONTROLLER_INPUT_TRANSFORM_FIXED,
        .flags = CONTROLLER_INPUT_BINDING_NONE,
        .action = {
            .kind = CONTROLLER_ACTION_SYSTEM,
            .value.system = CONTROLLER_SYSTEM_ACTION_START_PROVISIONING,
        },
    };
    s_active_bindings = &unlocked_system_binding;
    s_active_binding_count = 1;
    controller_action_t action = controller_action_none();
    assert(!controller_input_resolve_physical(
        &boot, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));

    static const controller_input_descriptor_t unlocked_system_descriptor = {
        .source_id = CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS,
        .control_id = CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
        .flags = CONTROLLER_INPUT_DESCRIPTOR_NONE,
        .allowed_gestures = CONTROLLER_PHYSICAL_GESTURE_MASK(
            CONTROLLER_PHYSICAL_GESTURE_HOLD),
        .allowed_event_flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
        .value_policy = CONTROLLER_INPUT_VALUE_UNIT_POSITIVE,
        .label = "Unlocked BOOT",
    };
    s_active_bindings = &s_test_bindings[3];
    s_active_binding_count = 1;
    s_active_descriptors = &unlocked_system_descriptor;
    s_active_descriptor_count = 1;
    assert(!controller_input_resolve_physical(
        &boot, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));

    reset_profiles();
}

static void assert_control_command(controller_control_intent_t intent,
                                   controller_interaction_context_t context,
                                   controller_command_kind_t kind,
                                   int32_t steps) {
    controller_action_t action = controller_action_none();
    assert(controller_input_resolve_control(intent, context, &action));
    assert_command(&action, kind, steps);
}

static void test_semantic_control_context_matrix(void) {
    const controller_interaction_context_t non_picker[] = {
        CONTROLLER_INTERACTION_CONTEXT_MEDIA,
        CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
    };
    for (size_t i = 0;
         i < sizeof(non_picker) / sizeof(non_picker[0]); ++i) {
        controller_interaction_context_t context = non_picker[i];
        assert_control_command(CONTROLLER_CONTROL_INTENT_VOLUME_UP, context,
                               CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, 1);
        assert_control_command(CONTROLLER_CONTROL_INTENT_VOLUME_DOWN, context,
                               CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, -1);
        assert_control_command(CONTROLLER_CONTROL_INTENT_ACTIVATE, context,
                               CONTROLLER_COMMAND_TOGGLE_PLAYBACK, 0);
        assert_control_command(CONTROLLER_CONTROL_INTENT_NEXT, context,
                               CONTROLLER_COMMAND_NEXT_TRACK, 0);
        assert_control_command(CONTROLLER_CONTROL_INTENT_PREVIOUS, context,
                               CONTROLLER_COMMAND_PREVIOUS_TRACK, 0);
    }

    controller_action_t action = controller_action_none();
    assert(controller_input_resolve_control(
        CONTROLLER_CONTROL_INTENT_VOLUME_UP,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER, &action));
    assert(action.kind == CONTROLLER_ACTION_SCROLL_ZONE_PICKER);
    assert(action.value.picker_delta == 1);

    assert(controller_input_resolve_control(
        CONTROLLER_CONTROL_INTENT_VOLUME_DOWN,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER, &action));
    assert(action.value.picker_delta == -1);

    assert(controller_input_resolve_control(
        CONTROLLER_CONTROL_INTENT_ACTIVATE,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER, &action));
    assert(action.kind == CONTROLLER_ACTION_SELECT_ZONE_PICKER);

    assert(!controller_input_resolve_control(
        CONTROLLER_CONTROL_INTENT_NEXT,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER, &action));
    assert(!controller_input_resolve_control(
        CONTROLLER_CONTROL_INTENT_PREVIOUS,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER, &action));
}

static void test_fail_closed_values_and_copied_context(void) {
    controller_action_t action = controller_action_command(
        controller_command_adjust_volume(99));
    controller_physical_event_t event = dial_rotation(2);

    assert(!controller_input_set_context(
        (controller_interaction_context_t)99));
    assert(controller_input_get_context() ==
           CONTROLLER_INTERACTION_CONTEXT_MEDIA);
    assert(!controller_input_resolve_physical(
        NULL, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    assert(action.kind == CONTROLLER_ACTION_NONE);
    action = controller_action_command(controller_command_adjust_volume(99));
    assert(!controller_input_resolve_physical(
        &event, (controller_interaction_context_t)99, &action));
    assert(action.kind == CONTROLLER_ACTION_NONE);

    event.source_id = 999;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    event = dial_rotation(1);
    event.control_id = CONTROLLER_INPUT_CONTROL_NONE;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    event = dial_rotation(1);
    event.kind = (controller_physical_event_kind_t)99;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    event = dial_rotation(1);
    event.gesture = CONTROLLER_PHYSICAL_GESTURE_HOLD;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    event = dial_rotation(1);
    event.flags = CONTROLLER_PHYSICAL_EVENT_FLAG_REPEAT;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    event = dial_rotation(1);
    event.flags = (controller_physical_event_flags_t)(1u << 7);
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    event = frame_long_press(CONTROLLER_INPUT_CONTROL_FRAME_BOOT);
    event.value = 0;
    assert(!controller_input_resolve_physical(
        &event, CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));

    assert(!controller_input_resolve_control(
        CONTROLLER_CONTROL_INTENT_NONE,
        CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    assert(!controller_input_resolve_control(
        (controller_control_intent_t)99,
        CONTROLLER_INTERACTION_CONTEXT_MEDIA, &action));
    assert(action.kind == CONTROLLER_ACTION_NONE);

    controller_physical_event_t copied = dial_rotation(2);
    controller_interaction_context_t snapshot =
        controller_input_get_context();
    assert(controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER));
    assert(controller_input_resolve_physical(&copied, snapshot, &action));
    assert_command(&action, CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, 3);
}

static void test_dispatch_and_queue_contract(void) {
    reset_actions();
    controller_input_reset_control_mailbox_stats();

    controller_physical_event_t event = dial_rotation(-2);
    assert(controller_input_dispatch_physical(&event));
    assert(s_action_count == 1);
    assert_command(&s_actions[0], CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, -3);

    assert(controller_input_dispatch_control(
        CONTROLLER_CONTROL_INTENT_ACTIVATE));
    assert(s_action_count == 2);
    assert_command(&s_actions[1], CONTROLLER_COMMAND_TOGGLE_PLAYBACK, 0);

    s_action_count = 0;
    for (size_t i = 0; i < EXPECTED_QUEUE_CAPACITY; ++i) {
        controller_control_intent_t intent =
            (i % 2 == 0) ? CONTROLLER_CONTROL_INTENT_VOLUME_DOWN
                         : CONTROLLER_CONTROL_INTENT_VOLUME_UP;
        assert(controller_input_post_control(intent));
    }
    assert(!controller_input_post_control(
        CONTROLLER_CONTROL_INTENT_ACTIVATE));
    assert(!controller_input_post_control(CONTROLLER_CONTROL_INTENT_NONE));
    controller_input_mailbox_stats_t full_stats =
        controller_input_control_mailbox_stats();
    assert(full_stats.accepted == EXPECTED_QUEUE_CAPACITY);
    assert(full_stats.coalesced == 0);
    assert(full_stats.dropped == 1);
    assert(full_stats.overflow == 1);
    platform_task_run_pending();

    assert(s_action_count == EXPECTED_QUEUE_CAPACITY);
    for (size_t i = 0; i < EXPECTED_QUEUE_CAPACITY; ++i) {
        int32_t expected = (i % 2 == 0) ? -1 : 1;
        assert_command(&s_actions[i],
                       CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, expected);
    }

    controller_input_reset_control_mailbox_stats();
    s_action_count = 0;
    assert(controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_MEDIA));
    assert(controller_input_post_control(
        CONTROLLER_CONTROL_INTENT_ACTIVATE));
    assert(controller_input_set_context(
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER));
    platform_task_run_pending();
    assert(s_action_count == 1);
    assert(s_actions[0].kind == CONTROLLER_ACTION_SELECT_ZONE_PICKER);
    controller_input_mailbox_stats_t context_stats =
        controller_input_control_mailbox_stats();
    assert(context_stats.accepted == 1);
    assert(context_stats.dropped == 0);

    s_action_count = 0;
    controller_action_t adaptive =
        controller_action_simple(CONTROLLER_ACTION_ADAPTIVE_REF);
    adaptive.value.adaptive_ref = 7;
    assert(!controller_input_dispatch_action(&adaptive));
    controller_action_t invalid_command = controller_action_command(
        controller_command_make((controller_command_kind_t)99));
    assert(!controller_input_dispatch_action(&invalid_command));
    controller_action_t invalid_system = controller_action_system(
        (controller_system_action_t)99);
    assert(!controller_input_dispatch_action(&invalid_system));
    controller_action_t forged_system = controller_action_system(
        CONTROLLER_SYSTEM_ACTION_RESTART);
    assert(!controller_input_dispatch_action(&forged_system));
    assert(s_action_count == 0);
}

int main(void) {
    reset_actions();
    test_dial_rotation_resolution();
    test_frame_locked_defaults_and_unused_pwr();
    test_public_dispatch_rejects_system_actions();
    test_semantic_control_context_matrix();
    test_fail_closed_values_and_copied_context();
    test_dispatch_and_queue_contract();
    puts("controller input contracts passed");
    return 0;
}
