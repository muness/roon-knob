#pragma once

#include <stdarg.h>

void platform_log_backend(const char *level, const char *fmt, va_list args);

static inline void platform_log_impl(const char *level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    platform_log_backend(level, fmt, args);
    va_end(args);
}

#define LOGI(...) platform_log_impl("I", __VA_ARGS__)
#define LOGD(...) platform_log_impl("D", __VA_ARGS__)
#define LOGW(...) platform_log_impl("W", __VA_ARGS__)
#define LOGE(...) platform_log_impl("E", __VA_ARGS__)
