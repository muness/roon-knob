// Tough has no physical descriptors or bindings at all (touch bypasses this
// seam entirely -- see tough_app/main/controller_input_profile_tough.c).
// This is deliberately a standalone test, not a TEST_TOUGH_PROFILE branch of
// tests/test_controller_input_profile.c: that shared file's DIAL/FRAME
// branches assert specific non-empty descriptor/binding content, which does
// not apply here.

#include "controller_input_profile.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void) {
    size_t descriptor_count = 123;  // Sentinel to prove it gets overwritten.
    const controller_input_descriptor_t *descriptors =
        controller_input_profile_descriptors(&descriptor_count);
    assert(descriptors == NULL);
    assert(descriptor_count == 0);

    size_t binding_count = 456;
    const controller_input_binding_t *bindings =
        controller_input_profile_bindings(&binding_count);
    assert(bindings == NULL);
    assert(binding_count == 0);

    // NULL out_count must not crash.
    assert(controller_input_profile_descriptors(NULL) == NULL);
    assert(controller_input_profile_bindings(NULL) == NULL);

    puts("Tough input profile contracts passed");
    return 0;
}
