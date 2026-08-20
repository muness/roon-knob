#pragma once

#include <stdbool.h>

void frame_power_manager_init(void);
void frame_power_manager_poll(bool runtime_transition_pending);
