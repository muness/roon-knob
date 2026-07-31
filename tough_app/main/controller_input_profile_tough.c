// controller_input_profile.h implementation for M5Stack Tough.
//
// Tough has no physical descriptors or bindings: there is no rotary encoder
// and no software-visible physical button (see tough_capabilities.h). Touch
// input bypasses this seam entirely, exactly like Dial's touch handling in
// common/ui.c -- LVGL widget callbacks in touch_ui.c call
// controller_input_dispatch_action()/controller_action_router directly
// rather than going through a controller_physical_event_t descriptor/binding
// pair. Do NOT invent fake physical controls to populate this file.

#include "controller_input_profile.h"

#include <stddef.h>

// A zero-length array initializer (`{}`) is a GNU extension that -pedantic
// rejects under this repo's -std=c11 -Werror -pedantic test build; NULL with
// out_count=0 is the standard-C-clean way to express "no entries" here.

const controller_input_descriptor_t *controller_input_profile_descriptors(
    size_t *out_count) {
    if (out_count) {
        *out_count = 0;
    }
    return NULL;
}

const controller_input_binding_t *controller_input_profile_bindings(
    size_t *out_count) {
    if (out_count) {
        *out_count = 0;
    }
    return NULL;
}
