// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "controller_device_settings.h"

#include <string.h>
#include "os_critical.h"
#include "controller_config.h"
#include "platform/platform_task.h"

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

/* Configuration-owned desired state. Effects run only on the UI actor. A
 * revision is admitted before calling the adapter; no lock spans NVS/hardware. */
static os_critical_t s_settings_lock = OS_CRITICAL_INITIALIZER;
static controller_device_settings_t s_pending;
static uint32_t s_revision, s_endpoint_generation, s_local_mask;
static int s_retries_left;
static bool s_wake_queued;
static controller_device_settings_status_t s_status;

static void settings_on_ui(void *unused) {
    (void)unused;
    os_critical_enter(&s_settings_lock);
    s_wake_queued = false;
    controller_device_settings_t desired = s_pending;
    uint32_t revision = s_revision, endpoint = s_endpoint_generation;
    uint32_t local_mask = s_local_mask;
    os_critical_leave(&s_settings_lock);
    if (!desired.present) return;
    controller_config_endpoint_token_t cfg = {0};
    bool current = controller_config_capture_endpoint_token(&cfg);
    if (!current || cfg.generation != endpoint) desired.present &= local_mask;
    os_critical_enter(&s_settings_lock);
    if (revision != s_revision) { os_critical_leave(&s_settings_lock); return; }
    s_pending.present = desired.present;
    if (!desired.present) s_status = CDS_STATUS_CANCELLED;
    os_critical_leave(&s_settings_lock);
    if (!desired.present) return;
    bool applied = controller_device_settings_apply(&desired);
    os_critical_enter(&s_settings_lock);
    if (revision == s_revision) {
        if (applied) { s_pending.present = 0; s_local_mask = 0; s_status = CDS_STATUS_APPLIED; }
        else if (--s_retries_left <= 0) {
            s_pending.present = 0; s_local_mask = 0; s_status = CDS_STATUS_EXHAUSTED;
        }
    }
    os_critical_leave(&s_settings_lock);
}

static bool wake_settings(void) {
    os_critical_enter(&s_settings_lock);
    bool queue = s_pending.present && !s_wake_queued;
    if (queue) s_wake_queued = true;
    os_critical_leave(&s_settings_lock);
    if (!queue) return true;
    if (platform_task_post_to_ui(settings_on_ui, NULL)) return true;
    os_critical_enter(&s_settings_lock);
    s_wake_queued = false;
    os_critical_leave(&s_settings_lock);
    return false;
}

static void copy_fields(controller_device_settings_t *to,
                        const controller_device_settings_t *from) {
    if (from->present & CDS_SHOW_IP) to->show_ip = from->show_ip;
    if (from->present & CDS_BRIGHTNESS) to->brightness = from->brightness;
    if (from->present & CDS_BODY_ENABLED) to->body_enabled = from->body_enabled;
    if (from->present & CDS_SOUND_ENABLED) to->sound_enabled = from->sound_enabled;
    if (from->present & CDS_SOUND_VOLUME) to->sound_volume = from->sound_volume;
}

void controller_device_settings_submit(const controller_device_settings_t *settings,
                                        uint32_t endpoint_generation) {
    os_critical_enter(&s_settings_lock);
    /* Every accepted full snapshot supersedes previous remote fields, including
     * an empty projection. A queued explicit local edit still runs first. */
    s_pending.present &= s_local_mask;
    controller_device_settings_t remote = settings ? *settings : (controller_device_settings_t){0};
    remote.present &= ~s_local_mask;
    copy_fields(&s_pending, &remote);
    s_pending.present |= remote.present;
    s_endpoint_generation = endpoint_generation;
    ++s_revision;
    s_retries_left = 1 + CONTROLLER_DEVICE_SETTINGS_MAX_RETRIES;
    s_status = s_pending.present ? CDS_STATUS_PENDING : CDS_STATUS_CANCELLED;
    os_critical_leave(&s_settings_lock);
    (void)wake_settings();
}

/* Called by direct UI edits before their effects. The UI actor cannot be
 * executing a remote adapter concurrently with this local mutation. */
void controller_device_settings_local_edit(uint32_t fields) {
    os_critical_enter(&s_settings_lock);
    s_pending.present &= ~fields;
    s_local_mask &= ~fields;
    ++s_revision;
    if (!s_pending.present) s_status = CDS_STATUS_CANCELLED;
    os_critical_leave(&s_settings_lock);
}

/* Portal callers enqueue local effects into the same actor order. */
bool controller_device_settings_submit_local(const controller_device_settings_t *settings) {
    if (!settings || !settings->present) return true;
    os_critical_enter(&s_settings_lock);
    copy_fields(&s_pending, settings);
    s_pending.present |= settings->present;
    s_local_mask |= settings->present;
    ++s_revision;
    s_retries_left = 1 + CONTROLLER_DEVICE_SETTINGS_MAX_RETRIES;
    s_status = CDS_STATUS_PENDING;
    os_critical_leave(&s_settings_lock);
    return wake_settings();
}

void controller_device_settings_retry_pending(void) { (void)wake_settings(); }
controller_device_settings_status_t controller_device_settings_status(void) {
    os_critical_enter(&s_settings_lock);
    controller_device_settings_status_t status = s_status;
    os_critical_leave(&s_settings_lock);
    return status;
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
