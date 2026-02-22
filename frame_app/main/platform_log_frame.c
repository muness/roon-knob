// ESP-IDF backend for platform_log.h
// Overrides the weak default in common/platform/platform_log.c

#include "platform/platform_log.h"

#include <esp_log.h>
#include <stdio.h>

void platform_log_backend(const char *level, const char *fmt, va_list args) {
    // Route to ESP_LOG at INFO level (bridge_client uses LOGI/LOGW/LOGE)
    // The level string is "I", "W", or "E"
    esp_log_level_t esp_level = ESP_LOG_INFO;
    if (level && level[0] == 'E') {
        esp_level = ESP_LOG_ERROR;
    } else if (level && level[0] == 'W') {
        esp_level = ESP_LOG_WARN;
    }
    esp_log_writev(esp_level, "rk", fmt, args);
}
