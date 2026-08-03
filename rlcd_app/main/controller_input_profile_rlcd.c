#include "controller_input_profile.h"

#define RLCD_BIND(input_control, input_gesture, command_kind, command_steps) \
    { .source_id = CONTROLLER_INPUT_SOURCE_RLCD_BUTTONS, .control_id = (input_control), \
      .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON, .gesture = (input_gesture), \
      .context = CONTROLLER_INTERACTION_CONTEXT_MEDIA, \
      .transform = CONTROLLER_INPUT_TRANSFORM_FIXED, \
      .flags = CONTROLLER_INPUT_BINDING_LOCKED_DEFAULT, \
      .action = { .kind = CONTROLLER_ACTION_COMMAND, \
                  .value.command = { .kind = (command_kind), .volume_steps = (command_steps) } } }

#define RLCD_PICKER_BIND(input_control, input_gesture, input_context, action_kind, delta) \
    { .source_id = CONTROLLER_INPUT_SOURCE_RLCD_BUTTONS, .control_id = (input_control), \
      .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON, .gesture = (input_gesture), \
      .context = (input_context), .transform = CONTROLLER_INPUT_TRANSFORM_FIXED, \
      .flags = CONTROLLER_INPUT_BINDING_LOCKED_DEFAULT, \
      .action = { .kind = (action_kind), .value.picker_delta = (delta) } }

static const controller_input_descriptor_t s_descriptors[] = {
    { .source_id = CONTROLLER_INPUT_SOURCE_RLCD_BUTTONS,
      .control_id = CONTROLLER_INPUT_CONTROL_RLCD_BOOT,
      .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
      .flags = CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT,
      .allowed_gestures = CONTROLLER_PHYSICAL_GESTURE_MASK(CONTROLLER_PHYSICAL_GESTURE_TAP) |
                          CONTROLLER_PHYSICAL_GESTURE_MASK(CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP),
      .allowed_event_flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
      .value_policy = CONTROLLER_INPUT_VALUE_UNIT_POSITIVE, .label = "RLCD BOOT" },
    { .source_id = CONTROLLER_INPUT_SOURCE_RLCD_BUTTONS,
      .control_id = CONTROLLER_INPUT_CONTROL_RLCD_KEY,
      .event_kind = CONTROLLER_PHYSICAL_EVENT_BUTTON,
      .flags = CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT,
      .allowed_gestures = CONTROLLER_PHYSICAL_GESTURE_MASK(CONTROLLER_PHYSICAL_GESTURE_TAP) |
                          CONTROLLER_PHYSICAL_GESTURE_MASK(CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP),
      .allowed_event_flags = CONTROLLER_PHYSICAL_EVENT_FLAG_NONE,
      .value_policy = CONTROLLER_INPUT_VALUE_UNIT_POSITIVE, .label = "RLCD KEY" },
};

static bool s_key_track_mode;
static controller_input_binding_t s_bindings[] = {
    RLCD_BIND(CONTROLLER_INPUT_CONTROL_RLCD_BOOT, CONTROLLER_PHYSICAL_GESTURE_TAP,
              CONTROLLER_COMMAND_TOGGLE_PLAYBACK, 0),
    RLCD_PICKER_BIND(CONTROLLER_INPUT_CONTROL_RLCD_BOOT,
                     CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
                     CONTROLLER_INTERACTION_CONTEXT_MEDIA,
                     CONTROLLER_ACTION_OPEN_ZONE_PICKER, 0),
    RLCD_BIND(CONTROLLER_INPUT_CONTROL_RLCD_KEY, CONTROLLER_PHYSICAL_GESTURE_TAP,
              CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, -1),
    RLCD_BIND(CONTROLLER_INPUT_CONTROL_RLCD_KEY, CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
              CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS, 1),
    RLCD_PICKER_BIND(CONTROLLER_INPUT_CONTROL_RLCD_KEY,
                     CONTROLLER_PHYSICAL_GESTURE_TAP,
                     CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                     CONTROLLER_ACTION_SCROLL_ZONE_PICKER, 1),
    RLCD_PICKER_BIND(CONTROLLER_INPUT_CONTROL_RLCD_KEY,
                     CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
                     CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                     CONTROLLER_ACTION_SCROLL_ZONE_PICKER, -1),
    RLCD_PICKER_BIND(CONTROLLER_INPUT_CONTROL_RLCD_BOOT,
                     CONTROLLER_PHYSICAL_GESTURE_TAP,
                     CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                     CONTROLLER_ACTION_CLOSE_ZONE_PICKER, 0),
    RLCD_PICKER_BIND(CONTROLLER_INPUT_CONTROL_RLCD_BOOT,
                     CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
                     CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER,
                     CONTROLLER_ACTION_SELECT_ZONE_PICKER, 0),
};

const controller_input_descriptor_t *controller_input_profile_descriptors(size_t *count) {
    if (count) *count = sizeof(s_descriptors) / sizeof(s_descriptors[0]);
    return s_descriptors;
}
const controller_input_binding_t *controller_input_profile_bindings(size_t *count) {
    if (count) *count = sizeof(s_bindings) / sizeof(s_bindings[0]);
    return s_bindings;
}

void controller_input_profile_rlcd_set_key_mode(bool track_mode) {
    s_key_track_mode = track_mode;
    s_bindings[2].action.value.command.kind = track_mode
        ? CONTROLLER_COMMAND_PREVIOUS_TRACK
        : CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS;
    s_bindings[2].action.value.command.volume_steps = track_mode ? 0 : -1;
    s_bindings[3].action.value.command.kind = track_mode
        ? CONTROLLER_COMMAND_NEXT_TRACK
        : CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS;
    s_bindings[3].action.value.command.volume_steps = track_mode ? 0 : 1;
}
