#pragma once

#include <stdbool.h>

/*
 * Target-owned provisioning service lifecycle.  Shared WiFi orchestration
 * must not depend on a target's captive portal implementation directly.
 */
bool platform_provisioning_start(void);
void platform_provisioning_stop(void);
