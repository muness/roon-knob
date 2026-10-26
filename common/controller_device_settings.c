// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"

#include <string.h>

static bool optional_bool(const cJSON *config, const char *key, bool *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(config, key);
    if (!cJSON_IsBool(item)) return false;
    *out = cJSON_IsTrue(item);
    return true;
}

static bool optional_int_in_range(const cJSON *config, const char *key, int min, int max,
                                  uint8_t *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(config, key);
    if (!cJSON_IsNumber(item)) return false;
    double value = item->valuedouble;
    /* The range test also rejects NaN and infinities, so the int cast below is always defined. */
    if (!(value >= min && value <= max) || value != (double)(int)value) return false;
    *out = (uint8_t)(int)value;
    return true;
}

bool controller_device_settings_parse(const cJSON *config, controller_device_settings_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(config)) return false;
    if (optional_bool(config, "show_ip", &out->show_ip)) out->present |= CDS_SHOW_IP;
    if (optional_int_in_range(config, "brightness", CONTROLLER_DEVICE_BRIGHTNESS_MIN,
                              CONTROLLER_DEVICE_BRIGHTNESS_MAX, &out->brightness))
        out->present |= CDS_BRIGHTNESS;
    if (optional_bool(config, "body_enabled", &out->body_enabled)) out->present |= CDS_BODY_ENABLED;
    if (optional_bool(config, "sound_enabled", &out->sound_enabled))
        out->present |= CDS_SOUND_ENABLED;
    if (optional_int_in_range(config, "sound_volume", 0, CONTROLLER_DEVICE_SOUND_VOLUME_MAX,
                              &out->sound_volume))
        out->present |= CDS_SOUND_VOLUME;
    return out->present != 0;
}

bool controller_device_settings_apply(const controller_device_settings_t *settings) {
    if (!settings || settings->present == 0) return true;
    return platform_device_settings_apply(settings);
}

static controller_device_settings_t s_pending;
static int s_retries_left;

void controller_device_settings_submit(const controller_device_settings_t *settings) {
    /* An empty submission says nothing about a change that is still pending. */
    if (!settings || settings->present == 0) return;
    /* The bridge serves its full current set each time, so a newer submission supersedes. */
    memset(&s_pending, 0, sizeof(s_pending));
    s_retries_left = 0;
    if (controller_device_settings_apply(settings)) return;
    s_pending = *settings;
    s_retries_left = CONTROLLER_DEVICE_SETTINGS_MAX_RETRIES;
}

void controller_device_settings_retry_pending(void) {
    if (s_pending.present == 0) return;
    if (controller_device_settings_apply(&s_pending)) {
        memset(&s_pending, 0, sizeof(s_pending));
        s_retries_left = 0;
        return;
    }
    if (--s_retries_left <= 0) memset(&s_pending, 0, sizeof(s_pending));
}

bool controller_firmware_update_parse(const cJSON *config, char *from_version, size_t capacity) {
    if (from_version && capacity) from_version[0] = '\0';
    if (!from_version || capacity == 0 || !cJSON_IsObject(config)) return false;
    const cJSON *request = cJSON_GetObjectItemCaseSensitive(config, "firmware_update");
    if (!cJSON_IsObject(request)) return false;
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(request, "action");
    const cJSON *from = cJSON_GetObjectItemCaseSensitive(request, "from_version");
    if (!cJSON_IsString(action) || !action->valuestring || strcmp(action->valuestring, "install") != 0)
        return false;
    if (!cJSON_IsString(from) || !from->valuestring) return false;
    size_t length = strlen(from->valuestring);
    if (length >= capacity) return false;
    memcpy(from_version, from->valuestring, length + 1);
    return true;
}
