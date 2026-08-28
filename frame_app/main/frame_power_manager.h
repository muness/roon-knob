#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform_power.h"

void frame_power_manager_init(void);
void frame_power_manager_poll(bool runtime_transition_pending);
void frame_power_manager_debug_enrich(platform_power_diagnostics_t *out);
bool frame_power_manager_debug_arm(uint32_t delay_sec);
