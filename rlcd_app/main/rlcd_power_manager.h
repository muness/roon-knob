#pragma once

#include "platform/platform_power.h"

#include <stdbool.h>
#include <stdint.h>

void rlcd_power_manager_init(void);
void rlcd_power_manager_poll(bool runtime_transition_pending);
void rlcd_power_manager_debug_enrich(platform_power_diagnostics_t *out);
bool rlcd_power_manager_debug_arm(uint32_t delay_sec);
