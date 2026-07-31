#pragma once

#include "controller_input.h"

void platform_input_init(void);
void platform_input_process_events(void);  // Call from main loop to dispatch queued input events
void platform_input_shutdown(void);
controller_input_mailbox_stats_t platform_input_mailbox_stats(void);
