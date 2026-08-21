#include "platform/platform_power.h"

#include "controller_config.h"
#include "platform/platform_display.h"
#include "platform/platform_log.h"
#include "platform/platform_time.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_err.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <nvs.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#if defined(__GNUC__)
#  define PLATFORM_WEAK __attribute__((weak))
#else
#  define PLATFORM_WEAK
#endif

#ifdef ESP_PLATFORM
#define POWER_EVIDENCE_MAGIC 0x50575232u /* PWR2 */
#define POWER_EVIDENCE_VERSION 3u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t boots;
    uint32_t brownouts;
    uint32_t attempts;
    uint32_t preflights;
    uint32_t entries;
    uint32_t hardware_wakes;
    uint32_t last_flags;
    uint32_t last_error;
    int32_t last_reset_reason;
    int32_t last_wakeup_cause;
    int32_t last_entry_battery_level;
    uint8_t entry_pending;
    uint8_t last_boot_followed_entry;
    uint8_t trace_count;
    uint8_t trace_next;
    uint32_t next_trace_sequence;
    platform_power_trace_event_t trace[PLATFORM_POWER_TRACE_MAX_EVENTS];
} power_evidence_record_t;

static const char *POWER_EVIDENCE_NAMESPACE = "pwr_evidence";
static const char *POWER_EVIDENCE_KEY = "journal_v3";
static SemaphoreHandle_t s_power_evidence_mutex;
static bool s_power_evidence_boot_recorded;

static bool power_evidence_lock(void) {
    if (!s_power_evidence_mutex) {
        s_power_evidence_mutex = xSemaphoreCreateMutex();
    }
    return s_power_evidence_mutex &&
        xSemaphoreTake(s_power_evidence_mutex, portMAX_DELAY) == pdTRUE;
}

static void power_evidence_unlock(void) {
    xSemaphoreGive(s_power_evidence_mutex);
}

static void power_evidence_default(power_evidence_record_t *record) {
    memset(record, 0, sizeof(*record));
    record->magic = POWER_EVIDENCE_MAGIC;
    record->version = POWER_EVIDENCE_VERSION;
    record->last_entry_battery_level = -1;
}

static void power_evidence_load(power_evidence_record_t *record) {
    power_evidence_default(record);
    nvs_handle_t handle;
    if (nvs_open(POWER_EVIDENCE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t size = sizeof(*record);
    power_evidence_record_t stored;
    if (nvs_get_blob(handle, POWER_EVIDENCE_KEY, &stored, &size) == ESP_OK &&
        size == sizeof(stored) && stored.magic == POWER_EVIDENCE_MAGIC &&
        stored.version == POWER_EVIDENCE_VERSION &&
        stored.trace_count <= PLATFORM_POWER_TRACE_MAX_EVENTS &&
        stored.trace_next < PLATFORM_POWER_TRACE_MAX_EVENTS) {
        *record = stored;
    }
    nvs_close(handle);
}

static bool power_evidence_save(const power_evidence_record_t *record) {
    nvs_handle_t handle;
    if (nvs_open(POWER_EVIDENCE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const esp_err_t set = nvs_set_blob(
        handle, POWER_EVIDENCE_KEY, record, sizeof(*record));
    const esp_err_t commit = set == ESP_OK ? nvs_commit(handle) : set;
    nvs_close(handle);
    return set == ESP_OK && commit == ESP_OK;
}

static void power_evidence_append(power_evidence_record_t *record,
                                  platform_power_trace_type_t type,
                                  int battery_level) {
    const uint8_t index = record->trace_next % PLATFORM_POWER_TRACE_MAX_EVENTS;
    platform_power_trace_event_t *event = &record->trace[index];
    memset(event, 0, sizeof(*event));
    event->sequence = ++record->next_trace_sequence;
    event->boot_id = record->boots;
    const uint64_t uptime = platform_millis();
    event->uptime_ms = uptime > UINT32_MAX ? UINT32_MAX : (uint32_t)uptime;
    event->preflight_flags = record->last_flags;
    event->preflight_error = record->last_error;
    event->battery_level = (int16_t)battery_level;
    event->reset_reason = (int8_t)record->last_reset_reason;
    event->wakeup_cause = (int8_t)record->last_wakeup_cause;
    event->type = (uint8_t)type;
    record->trace_next = (uint8_t)((index + 1) % PLATFORM_POWER_TRACE_MAX_EVENTS);
    if (record->trace_count < PLATFORM_POWER_TRACE_MAX_EVENTS) {
        record->trace_count++;
    }
}

void platform_power_evidence_note_boot(void) {
    if (!power_evidence_lock()) return;
    if (!s_power_evidence_boot_recorded) {
        power_evidence_record_t record;
        power_evidence_load(&record);
        const esp_reset_reason_t reset = esp_reset_reason();
        const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
        record.boots++;
        if (reset == ESP_RST_BROWNOUT) record.brownouts++;
        if (reset == ESP_RST_DEEPSLEEP &&
            wake != ESP_SLEEP_WAKEUP_UNDEFINED) {
            record.hardware_wakes++;
        }
        record.last_reset_reason = (int32_t)reset;
        record.last_wakeup_cause = (int32_t)wake;
        /* Preserve whether this boot ended a claimed terminal-power interval.
         * The reset/wake pair then distinguishes a hardware wake, brownout,
         * or later full power-on without losing that correlation. */
        record.last_boot_followed_entry = record.entry_pending;
        record.entry_pending = 0;
        power_evidence_append(&record, PLATFORM_POWER_TRACE_BOOT, -1);
        if (power_evidence_save(&record)) {
            s_power_evidence_boot_recorded = true;
        } else {
            LOGE("Could not persist power evidence boot event");
        }
    }
    power_evidence_unlock();
}

static void power_evidence_mutate(
    void (*mutate)(power_evidence_record_t *, int32_t), int32_t value,
    platform_power_trace_type_t type, int battery_level) {
    if (!power_evidence_lock()) return;
    power_evidence_record_t record;
    power_evidence_load(&record);
    mutate(&record, value);
    power_evidence_append(&record, type, battery_level);
    if (!power_evidence_save(&record)) {
        LOGE("Could not persist power evidence event type=%u", (unsigned)type);
    }
    power_evidence_unlock();
}

static void evidence_attempt(power_evidence_record_t *record, int32_t unused) {
    (void)unused;
    record->attempts++;
    record->last_flags = 0;
    record->last_error = PLATFORM_POWER_PREFLIGHT_ERROR_NONE;
}

static void evidence_error(power_evidence_record_t *record, int32_t error) {
    record->last_error = (uint32_t)error;
}

static void evidence_preflight(power_evidence_record_t *record, int32_t flags) {
    record->preflights++;
    record->last_flags = (uint32_t)flags;
    record->last_error = PLATFORM_POWER_PREFLIGHT_ERROR_NONE;
}

static void evidence_entry(power_evidence_record_t *record, int32_t battery) {
    record->entries++;
    record->entry_pending = 1;
    record->last_entry_battery_level = battery;
}

void platform_power_evidence_note_attempt(void) {
    power_evidence_mutate(evidence_attempt, 0,
                          PLATFORM_POWER_TRACE_ATTEMPT, -1);
}

void platform_power_evidence_note_error(uint32_t error) {
    power_evidence_mutate(evidence_error, (int32_t)error,
                          PLATFORM_POWER_TRACE_ERROR, -1);
}

void platform_power_evidence_note_preflight(uint32_t flags) {
    power_evidence_mutate(evidence_preflight, (int32_t)flags,
                          PLATFORM_POWER_TRACE_PREFLIGHT, -1);
}

void platform_power_evidence_note_entry(int battery_level) {
    power_evidence_mutate(evidence_entry, (int32_t)battery_level,
                          PLATFORM_POWER_TRACE_ENTRY, battery_level);
}

static uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static void power_evidence_enrich(platform_power_diagnostics_t *out) {
    platform_power_evidence_note_boot();
    if (!power_evidence_lock()) return;
    power_evidence_record_t record;
    power_evidence_load(&record);
    power_evidence_unlock();
    out->durable_boots = record.boots;
    out->durable_brownouts = record.brownouts;
    out->terminal_entry_pending = record.entry_pending != 0;
    out->last_boot_followed_terminal_entry =
        record.last_boot_followed_entry != 0;
    out->last_entry_battery_level = record.last_entry_battery_level;
    out->power_off_attempts = max_u32(out->power_off_attempts, record.attempts);
    out->preflight_completions = max_u32(
        out->preflight_completions, record.preflights);
    out->power_off_entries = max_u32(out->power_off_entries, record.entries);
    out->hardware_wakes = max_u32(out->hardware_wakes, record.hardware_wakes);
    if (record.attempts > 0) {
        out->last_preflight_flags = record.last_flags;
        out->last_preflight_error = record.last_error;
    }
    if (record.boots > 0) {
        out->reset_reason = record.last_reset_reason;
        out->wakeup_cause = record.last_wakeup_cause;
    }
    out->trace_event_count = record.trace_count;
    const uint8_t start = record.trace_count == PLATFORM_POWER_TRACE_MAX_EVENTS
        ? record.trace_next : 0;
    for (uint8_t i = 0; i < record.trace_count; ++i) {
        out->trace_events[i] = record.trace[
            (start + i) % PLATFORM_POWER_TRACE_MAX_EVENTS];
    }
}
#else
void platform_power_evidence_note_boot(void) {}
void platform_power_evidence_note_attempt(void) {}
void platform_power_evidence_note_error(uint32_t error) { (void)error; }
void platform_power_evidence_note_preflight(uint32_t flags) { (void)flags; }
void platform_power_evidence_note_entry(int battery_level) {
    (void)battery_level;
}
#endif

PLATFORM_WEAK void platform_power_diagnostics_enrich(
    platform_power_diagnostics_t *out) {
    (void)out;
}

PLATFORM_WEAK bool platform_power_debug_arm_sleep(uint32_t delay_sec) {
    (void)delay_sec;
    return false;
}

const char *platform_power_source_name(platform_power_source_t source) {
    switch (source) {
    case PLATFORM_POWER_SOURCE_BATTERY:
        return "battery";
    case PLATFORM_POWER_SOURCE_EXTERNAL:
        return "external";
    case PLATFORM_POWER_SOURCE_UNKNOWN:
    default:
        return "unknown";
    }
}

bool platform_power_prepare_for_deep_sleep(void) {
#ifdef ESP_PLATFORM
#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    esp_pm_config_t config = {0};
    if (esp_pm_get_configuration(&config) != ESP_OK) {
        return false;
    }
    if (config.light_sleep_enable) {
        config.light_sleep_enable = false;
        if (esp_pm_configure(&config) != ESP_OK) {
            return false;
        }
    }
#endif
    /* Automatic Light-sleep owns the timer wake source while enabled. Clear
     * every inherited source so it cannot turn a requested Deep-sleep into a
     * short timer nap. The target installs only its qualified wake sources
     * after this common preflight succeeds. */
    return esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) == ESP_OK;
#else
    return true;
#endif
}

void platform_power_diagnostics_snapshot(platform_power_diagnostics_t *out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->power.battery_level = -1;
    out->power.source = PLATFORM_POWER_SOURCE_UNKNOWN;
    platform_power_snapshot(&out->power);
    out->state = platform_display_is_sleeping()
        ? PLATFORM_POWER_STATE_DISPLAY_SLEEP
        : PLATFORM_POWER_STATE_ACTIVE;
    out->wifi_modem_sleep_baseline = true;
    out->uptime_ms = platform_millis();
#ifdef ESP_PLATFORM
    out->reset_reason = (int)esp_reset_reason();
    out->wakeup_cause = (int)esp_sleep_get_wakeup_cause();
#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    esp_pm_config_t pm_config = {0};
    if (esp_pm_get_configuration(&pm_config) == ESP_OK) {
        out->automatic_light_sleep_configured =
            pm_config.light_sleep_enable;
    }
#endif
#endif

    controller_config_snapshot_t config = {0};
    if (controller_config_snapshot(&config)) {
        const bool external = out->power.external_power;
        out->policy_known = true;
        out->art_timeout_sec =
            rk_cfg_get_art_mode_timeout(&config.value, external);
        out->dim_timeout_sec =
            rk_cfg_get_dim_timeout(&config.value, external);
        out->display_sleep_timeout_sec =
            rk_cfg_get_sleep_timeout(&config.value, external);
        out->power_off_timeout_sec =
            rk_cfg_get_deep_sleep_timeout(&config.value, external);
    }

    platform_power_diagnostics_enrich(out);
#ifdef ESP_PLATFORM
    power_evidence_enrich(out);
#endif
}
