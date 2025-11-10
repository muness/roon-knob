#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct platform_power_status {
    bool present;
    bool charging;
    int voltage_mv;
    int percentage;
};

/**
 * Initialize battery / power monitoring hardware. Safe to call multiple times.
 */
void platform_power_init(void);

/**
 * Sample the current battery / power state.
 *
 * @param out_status Destination for the latest reading.
 * @return true if a valid reading was produced, false otherwise.
 */
bool platform_power_get_status(struct platform_power_status *out_status);

#ifdef __cplusplus
}
#endif
