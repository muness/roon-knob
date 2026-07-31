#include "controller_input_profile.h"

static const controller_input_descriptor_t s_descriptors[] = {
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
};

static const controller_input_binding_t s_bindings[] = {
    {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .event_kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .context = CONTROLLER_INTERACTION_CONTEXT_MEDIA,
        .transform = CONTROLLER_INPUT_TRANSFORM_ROTATION_ACCELERATED,
        .flags = CONTROLLER_INPUT_BINDING_NONE,
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
        .flags = CONTROLLER_INPUT_BINDING_NONE,
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
        .flags = CONTROLLER_INPUT_BINDING_NONE,
        .action = {
            .kind = CONTROLLER_ACTION_COMMAND,
            .value.command = {
                .kind = CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS,
                .volume_steps = 1,
            },
        },
    },
};

const controller_input_descriptor_t *controller_input_profile_descriptors(
    size_t *out_count) {
    if (out_count) {
        *out_count = sizeof(s_descriptors) / sizeof(s_descriptors[0]);
    }
    return s_descriptors;
}

const controller_input_binding_t *controller_input_profile_bindings(
    size_t *out_count) {
    if (out_count) {
        *out_count = sizeof(s_bindings) / sizeof(s_bindings[0]);
    }
    return s_bindings;
}
