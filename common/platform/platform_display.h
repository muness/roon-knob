#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if display is currently sleeping
 * @return true if display is off/sleeping, false if on or dimmed
 */
bool platform_display_is_sleeping(void);

#ifdef __cplusplus
}
#endif
