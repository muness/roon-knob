// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"

/* Targets with no bridge-controllable device setting (Slate, Joy, Tough). The bridge only serves a
 * key to a device it applies to, so ignoring everything here is correct, not a gap. */
void platform_device_settings_apply(const controller_device_settings_t *settings) {
    (void)settings;
}
