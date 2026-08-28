#include "platform/platform_time.h"
#include "os_time.h"

#include <time.h>
#include <sys/time.h>

#ifdef ESP_PLATFORM
extern int64_t esp_timer_get_time(void);
#endif

uint64_t platform_millis(void) {
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}

int64_t platform_utc_now_ms(void) {
    struct timeval tv = {0};
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    /* Reject the ESP-IDF epoch default and other obviously unsynchronized
     * values. The first supported firmware predates this floor by years. */
    const time_t credible_after = (time_t)1704067200; /* 2024-01-01 UTC */
    if (tv.tv_sec < credible_after) {
        return 0;
    }
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void platform_sleep_ms(uint32_t ms) {
    os_sleep_ms(ms);
}

void platform_sleep_us(uint32_t us) {
    os_sleep_us(us);
}
