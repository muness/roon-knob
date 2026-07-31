#include "controller_input_profile.h"

#define FRAME_BINDING(control, interaction_context, system_action)              \
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

static const controller_input_descriptor_t s_descriptors[] = {
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

static const controller_input_binding_t s_bindings[] = {
    FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
                  CONTROLLER_INTERACTION_CONTEXT_MEDIA,
                  CONTROLLER_SYSTEM_ACTION_START_PROVISIONING),
    FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
                  CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                  CONTROLLER_SYSTEM_ACTION_START_PROVISIONING),
    FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_BOOT,
                  CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
                  CONTROLLER_SYSTEM_ACTION_START_PROVISIONING),
    FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_GP4,
                  CONTROLLER_INTERACTION_CONTEXT_MEDIA,
                  CONTROLLER_SYSTEM_ACTION_RESTART),
    FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_GP4,
                  CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                  CONTROLLER_SYSTEM_ACTION_RESTART),
    FRAME_BINDING(CONTROLLER_INPUT_CONTROL_FRAME_GP4,
                  CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY,
                  CONTROLLER_SYSTEM_ACTION_RESTART),
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
