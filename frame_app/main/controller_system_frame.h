#pragma once

#include "controller_input.h"

#include <stdbool.h>

void controller_system_frame_init(void);
void controller_system_frame_shutdown(void);

/* Only Frame's platform input adapter may execute a locked system binding. */
bool controller_system_frame_dispatch_physical(
    const controller_physical_event_t *event);

#if defined(CONTROLLER_SYSTEM_FRAME_TEST)
typedef struct {
    bool (*start_provisioning)(void);
    void (*restart)(void);
} controller_system_frame_effects_t;

void controller_system_frame_set_effects_for_test(
    const controller_system_frame_effects_t *effects);
#endif
