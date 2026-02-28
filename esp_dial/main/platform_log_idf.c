#include "platform/platform_log.h"

#include <esp_log.h>
#include <stdarg.h>

void platform_log_backend(const char *level, const char *fmt, va_list args) {
    esp_log_level_t lvl = ESP_LOG_INFO;
    if (level && level[0] == 'W') {
        lvl = ESP_LOG_WARN;
    } else if (level && level[0] == 'E') {
        lvl = ESP_LOG_ERROR;
    }
    esp_log_writev(lvl, "roon-knob", fmt, args);
}
