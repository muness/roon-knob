#pragma once

#include <stdint.h>

uint64_t platform_millis(void);
/** UTC Unix time in milliseconds, or 0 until the system clock is credible. */
int64_t platform_utc_now_ms(void);
void platform_sleep_ms(uint32_t ms);
void platform_sleep_us(uint32_t us);
