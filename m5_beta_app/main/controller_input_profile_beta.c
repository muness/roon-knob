#include "controller_input_profile.h"
#include <stddef.h>

const controller_input_descriptor_t *controller_input_profile_descriptors(size_t *count) {
    if (count) *count = 0;
    return NULL;
}
const controller_input_binding_t *controller_input_profile_bindings(size_t *count) {
    if (count) *count = 0;
    return NULL;
}
