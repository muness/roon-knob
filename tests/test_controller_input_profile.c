#include "controller_input_profile.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool descriptor_matches(const controller_input_descriptor_t *descriptor,
                               const controller_input_binding_t *binding) {
    return descriptor->source_id == binding->source_id &&
           descriptor->control_id == binding->control_id &&
           descriptor->event_kind == binding->event_kind;
}

static void assert_no_duplicate_bindings(
    const controller_input_binding_t *bindings,
    size_t count) {
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            bool same_key =
                bindings[i].source_id == bindings[j].source_id &&
                bindings[i].control_id == bindings[j].control_id &&
                bindings[i].event_kind == bindings[j].event_kind &&
                bindings[i].gesture == bindings[j].gesture &&
                bindings[i].context == bindings[j].context;
            assert(!same_key);
        }
    }
}

static void assert_bindings_have_descriptors(
    const controller_input_descriptor_t *descriptors,
    size_t descriptor_count,
    const controller_input_binding_t *bindings,
    size_t binding_count) {
    for (size_t i = 0; i < binding_count; ++i) {
        bool found = false;
        for (size_t j = 0; j < descriptor_count; ++j) {
            if (descriptor_matches(&descriptors[j], &bindings[i])) {
                found = true;
                break;
            }
        }
        assert(found);
    }
}

#if defined(TEST_DIAL_PROFILE)

static void test_profile(void) {
    size_t descriptor_count = 0;
    const controller_input_descriptor_t *descriptors =
        controller_input_profile_descriptors(&descriptor_count);
    assert(descriptors);
    assert(descriptor_count == 1);
    assert(descriptors[0].source_id ==
           CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN);
    assert(descriptors[0].control_id ==
           CONTROLLER_INPUT_CONTROL_DIAL_ROTATION);
    assert(descriptors[0].event_kind ==
           CONTROLLER_PHYSICAL_EVENT_ROTATION);
    assert(descriptors[0].allowed_gestures ==
           CONTROLLER_PHYSICAL_GESTURE_MASK(
               CONTROLLER_PHYSICAL_GESTURE_NONE));
    assert(descriptors[0].value_policy ==
           CONTROLLER_INPUT_VALUE_NONZERO_SIGNED_DELTA);
    assert(descriptors[0].flags ==
           CONTROLLER_INPUT_DESCRIPTOR_REBINDABLE);
    assert(strcmp(descriptors[0].label, "Dial encoder") == 0);

    size_t binding_count = 0;
    const controller_input_binding_t *bindings =
        controller_input_profile_bindings(&binding_count);
    assert(bindings);
    assert(binding_count == 3);
    bool contexts[3] = {false, false, false};
    for (size_t i = 0; i < binding_count; ++i) {
        assert(bindings[i].source_id ==
               CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN);
        assert(bindings[i].control_id ==
               CONTROLLER_INPUT_CONTROL_DIAL_ROTATION);
        assert(bindings[i].event_kind ==
               CONTROLLER_PHYSICAL_EVENT_ROTATION);
        assert(bindings[i].gesture ==
               CONTROLLER_PHYSICAL_GESTURE_NONE);
        assert(bindings[i].flags == CONTROLLER_INPUT_BINDING_NONE);
        assert(bindings[i].context >=
               CONTROLLER_INTERACTION_CONTEXT_MEDIA);
        assert(bindings[i].context <=
               CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY);
        contexts[bindings[i].context] = true;
        if (bindings[i].context ==
            CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER) {
            assert(bindings[i].transform ==
                   CONTROLLER_INPUT_TRANSFORM_ROTATION_DIRECTION);
            assert(bindings[i].action.kind ==
                   CONTROLLER_ACTION_SCROLL_ZONE_PICKER);
        } else {
            assert(bindings[i].transform ==
                   CONTROLLER_INPUT_TRANSFORM_ROTATION_ACCELERATED);
            assert(bindings[i].action.kind ==
                   CONTROLLER_ACTION_COMMAND);
            assert(bindings[i].action.value.command.kind ==
                   CONTROLLER_COMMAND_ADJUST_VOLUME_STEPS);
        }
    }
    assert(contexts[CONTROLLER_INTERACTION_CONTEXT_MEDIA]);
    assert(contexts[CONTROLLER_INTERACTION_CONTEXT_ZONE_PICKER]);
    assert(contexts[
        CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY]);
    assert_no_duplicate_bindings(bindings, binding_count);
    assert_bindings_have_descriptors(
        descriptors, descriptor_count, bindings, binding_count);
}

#elif defined(TEST_FRAME_PROFILE)

static void test_profile(void) {
    size_t descriptor_count = 0;
    const controller_input_descriptor_t *descriptors =
        controller_input_profile_descriptors(&descriptor_count);
    assert(descriptors);
    assert(descriptor_count == 3);
    assert(descriptors[0].control_id ==
           CONTROLLER_INPUT_CONTROL_FRAME_BOOT);
    assert(descriptors[0].flags ==
           CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT);
    assert(descriptors[0].event_kind == CONTROLLER_PHYSICAL_EVENT_BUTTON);
    assert(descriptors[0].allowed_gestures ==
           CONTROLLER_PHYSICAL_GESTURE_MASK(
               CONTROLLER_PHYSICAL_GESTURE_HOLD));
    assert(descriptors[0].value_policy ==
           CONTROLLER_INPUT_VALUE_UNIT_POSITIVE);
    assert(descriptors[1].control_id ==
           CONTROLLER_INPUT_CONTROL_FRAME_GP4);
    assert(descriptors[1].flags ==
           CONTROLLER_INPUT_DESCRIPTOR_LOCKED_DEFAULT);
    assert(descriptors[2].control_id ==
           CONTROLLER_INPUT_CONTROL_FRAME_PWR);
    assert(descriptors[2].flags == CONTROLLER_INPUT_DESCRIPTOR_NONE);
    assert(strcmp(descriptors[2].label, "Frame PWR (unused)") == 0);

    size_t binding_count = 0;
    const controller_input_binding_t *bindings =
        controller_input_profile_bindings(&binding_count);
    assert(bindings);
    assert(binding_count == 6);
    bool boot_contexts[3] = {false, false, false};
    bool gp4_contexts[3] = {false, false, false};
    for (size_t i = 0; i < binding_count; ++i) {
        assert(bindings[i].source_id ==
               CONTROLLER_INPUT_SOURCE_FRAME_BUTTONS);
        assert(bindings[i].event_kind ==
               CONTROLLER_PHYSICAL_EVENT_BUTTON);
        assert(bindings[i].gesture == CONTROLLER_PHYSICAL_GESTURE_HOLD);
        assert(bindings[i].transform ==
               CONTROLLER_INPUT_TRANSFORM_FIXED);
        assert(bindings[i].flags ==
               CONTROLLER_INPUT_BINDING_LOCKED_DEFAULT);
        assert(bindings[i].action.kind == CONTROLLER_ACTION_SYSTEM);
        assert(bindings[i].context >=
               CONTROLLER_INTERACTION_CONTEXT_MEDIA);
        assert(bindings[i].context <=
               CONTROLLER_INTERACTION_CONTEXT_SETTINGS_RECOVERY);
        if (bindings[i].control_id ==
            CONTROLLER_INPUT_CONTROL_FRAME_BOOT) {
            boot_contexts[bindings[i].context] = true;
            assert(bindings[i].action.value.system ==
                   CONTROLLER_SYSTEM_ACTION_START_PROVISIONING);
        } else {
            assert(bindings[i].control_id ==
                   CONTROLLER_INPUT_CONTROL_FRAME_GP4);
            gp4_contexts[bindings[i].context] = true;
            assert(bindings[i].action.value.system ==
                   CONTROLLER_SYSTEM_ACTION_RESTART);
        }
    }
    for (size_t i = 0; i < 3; ++i) {
        assert(boot_contexts[i]);
        assert(gp4_contexts[i]);
    }
    assert_no_duplicate_bindings(bindings, binding_count);
    assert_bindings_have_descriptors(
        descriptors, descriptor_count, bindings, binding_count);
}

#else
#error "Compile with TEST_DIAL_PROFILE or TEST_FRAME_PROFILE"
#endif

int main(void) {
    test_profile();
    puts("controller input profile contracts passed");
    return 0;
}
