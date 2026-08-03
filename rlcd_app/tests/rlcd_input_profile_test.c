#include "controller_input_profile.h"

#include <assert.h>
#include <stdio.h>

static const controller_input_binding_t *find_binding(
    const controller_input_binding_t *bindings, size_t count,
    uint16_t control_id, controller_physical_gesture_t gesture,
    controller_interaction_context_t context) {
    for (size_t i = 0; i < count; ++i) {
        if (bindings[i].control_id == control_id &&
            bindings[i].gesture == gesture &&
            bindings[i].context == context) return &bindings[i];
    }
    return NULL;
}

int main(void) {
    size_t count = 0;
    const controller_input_binding_t *bindings =
        controller_input_profile_bindings(&count);
    assert(bindings && count == 8);

    const controller_input_binding_t *next = find_binding(
        bindings, count, CONTROLLER_INPUT_CONTROL_RLCD_KEY,
        CONTROLLER_PHYSICAL_GESTURE_TAP,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER);
    const controller_input_binding_t *previous = find_binding(
        bindings, count, CONTROLLER_INPUT_CONTROL_RLCD_KEY,
        CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER);
    const controller_input_binding_t *cancel = find_binding(
        bindings, count, CONTROLLER_INPUT_CONTROL_RLCD_BOOT,
        CONTROLLER_PHYSICAL_GESTURE_TAP,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER);
    const controller_input_binding_t *select = find_binding(
        bindings, count, CONTROLLER_INPUT_CONTROL_RLCD_BOOT,
        CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
        CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER);

    assert(next && next->action.kind == CONTROLLER_ACTION_SCROLL_ZONE_PICKER);
    assert(next->action.value.picker_delta == 1);
    assert(previous && previous->action.kind == CONTROLLER_ACTION_SCROLL_ZONE_PICKER);
    assert(previous->action.value.picker_delta == -1);
    assert(cancel && cancel->action.kind == CONTROLLER_ACTION_CLOSE_ZONE_PICKER);
    assert(select && select->action.kind == CONTROLLER_ACTION_SELECT_ZONE_PICKER);

    controller_input_profile_rlcd_set_key_mode(true);
    bindings = controller_input_profile_bindings(&count);
    const controller_input_binding_t *track_prev = find_binding(
        bindings, count, CONTROLLER_INPUT_CONTROL_RLCD_KEY,
        CONTROLLER_PHYSICAL_GESTURE_TAP,
        CONTROLLER_INTERACTION_CONTEXT_MEDIA);
    const controller_input_binding_t *track_next = find_binding(
        bindings, count, CONTROLLER_INPUT_CONTROL_RLCD_KEY,
        CONTROLLER_PHYSICAL_GESTURE_DOUBLE_TAP,
        CONTROLLER_INTERACTION_CONTEXT_MEDIA);
    assert(track_prev && track_prev->action.value.command.kind ==
           CONTROLLER_COMMAND_PREVIOUS_TRACK);
    assert(track_next && track_next->action.value.command.kind ==
           CONTROLLER_COMMAND_NEXT_TRACK);
    controller_input_profile_rlcd_set_key_mode(false);
    bindings = controller_input_profile_bindings(&count);
    assert(find_binding(bindings, count, CONTROLLER_INPUT_CONTROL_RLCD_KEY,
                        CONTROLLER_PHYSICAL_GESTURE_TAP,
                        CONTROLLER_INTERACTION_CONTEXT_MEDIA)
               ->action.value.command.volume_steps == -1);
    puts("rlcd_input_profile_test: reversible picker bindings ok");
    return 0;
}
