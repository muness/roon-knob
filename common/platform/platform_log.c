#include "platform/platform_log.h"

#include <stdio.h>

#if defined(__GNUC__)
#  define PLATFORM_WEAK __attribute__((weak))
#else
#  define PLATFORM_WEAK
#endif

PLATFORM_WEAK void platform_log_backend(const char *level, const char *fmt, va_list args) {
    if (level && level[0] == 'D') {
        return;
    }
    fprintf(stderr, "[RK-%s] ", level);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}
