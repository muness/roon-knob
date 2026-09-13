#pragma once

#include "platform/platform_power.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared terminal-power contract for Tough, AtomS3 JoyStick, and all M5 beta
 * targets. The NVS record survives PMIC rail-off, unlike RTC memory. */
bool m5_terminal_power_off(void);
bool m5_terminal_power_debug_arm(uint32_t delay_sec);
bool m5_terminal_power_debug_due(void);
void m5_terminal_power_note_display_sleep(void);
void m5_terminal_power_note_runtime_wake(void);
void m5_terminal_power_diagnostics(platform_power_diagnostics_t *out);

#ifdef __cplusplus
}
#endif
