#include "platform/platform_power.h"

void platform_power_init(void) {}

bool platform_power_get_status(struct platform_power_status *out_status) {
    if (!out_status) {
        return false;
    }
    out_status->present = true;
    out_status->charging = false;
    out_status->voltage_mv = 4000;
    out_status->percentage = 100;
    return true;
}
