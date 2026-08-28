#include "platform/platform_power.h"

#include "controller_config.h"
#include "platform/platform_display.h"
#include "platform/platform_log.h"
#include "platform/platform_time.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_err.h>
#include <esp_pm.h>
#include <esp_random.h>
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
#define POWER_EVIDENCE_VERSION 4u

typedef struct {
    uint32_t sequence;
    uint32_t boot_id;
    uint32_t uptime_ms;
    uint32_t preflight_flags;
    uint32_t preflight_error;
    int16_t battery_level;
    int8_t reset_reason;
    int8_t wakeup_cause;
    uint8_t type;
    uint8_t reserved[3];
} power_evidence_event_v3_t;

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
    power_evidence_event_v3_t trace[PLATFORM_POWER_TRACE_MAX_EVENTS];
} power_evidence_record_v3_t;

_Static_assert(sizeof(power_evidence_event_v3_t) == 28,
               "v3 event migration layout changed");
_Static_assert(sizeof(power_evidence_record_v3_t) == 284,
               "v3 journal migration layout changed");

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
static const char *POWER_EVIDENCE_KEY = "journal_v4";
static const char *POWER_EVIDENCE_V3_KEY = "journal_v3";
static SemaphoreHandle_t s_power_evidence_mutex;
static bool s_power_evidence_boot_recorded;

#define POWER_EXPERIMENT_MAGIC 0x50584531u /* PXE1 */
#define POWER_EXPERIMENT_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t experiment_id;
    uint32_t state;
    uint32_t interval_sec;
    uint32_t maximum_duration_sec;
    uint32_t elapsed_sec;
    uint32_t next_sleep_sec;
    uint32_t total_samples;
    uint32_t intrusive_wakes;
    uint32_t next_sample_sequence;
    uint8_t observer_effect;
    uint8_t sample_count;
    uint8_t sample_next;
    uint8_t reserved;
    platform_power_experiment_sample_t
        samples[PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES];
} power_experiment_record_t;

static const char *POWER_EXPERIMENT_NAMESPACE = "pwr_experiment";
static const char *POWER_EXPERIMENT_KEY = "experiment_v1";

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
        nvs_close(handle);
        return;
    }

    power_evidence_record_v3_t legacy;
    size = sizeof(legacy);
    if (nvs_get_blob(handle, POWER_EVIDENCE_V3_KEY, &legacy, &size) == ESP_OK &&
        size == sizeof(legacy) && legacy.magic == POWER_EVIDENCE_MAGIC &&
        legacy.version == 3 &&
        legacy.trace_count <= PLATFORM_POWER_TRACE_MAX_EVENTS &&
        legacy.trace_next < PLATFORM_POWER_TRACE_MAX_EVENTS) {
        record->boots = legacy.boots;
        record->brownouts = legacy.brownouts;
        record->attempts = legacy.attempts;
        record->preflights = legacy.preflights;
        record->entries = legacy.entries;
        record->hardware_wakes = legacy.hardware_wakes;
        record->last_flags = legacy.last_flags;
        record->last_error = legacy.last_error;
        record->last_reset_reason = legacy.last_reset_reason;
        record->last_wakeup_cause = legacy.last_wakeup_cause;
        record->last_entry_battery_level = legacy.last_entry_battery_level;
        record->entry_pending = legacy.entry_pending;
        record->last_boot_followed_entry = legacy.last_boot_followed_entry;
        record->trace_count = legacy.trace_count;
        record->trace_next = legacy.trace_next;
        record->next_trace_sequence = legacy.next_trace_sequence;
        for (uint8_t i = 0; i < PLATFORM_POWER_TRACE_MAX_EVENTS; ++i) {
            record->trace[i].sequence = legacy.trace[i].sequence;
            record->trace[i].boot_id = legacy.trace[i].boot_id;
            record->trace[i].uptime_ms = legacy.trace[i].uptime_ms;
            record->trace[i].preflight_flags = legacy.trace[i].preflight_flags;
            record->trace[i].preflight_error = legacy.trace[i].preflight_error;
            record->trace[i].battery_level = legacy.trace[i].battery_level;
            record->trace[i].reset_reason = legacy.trace[i].reset_reason;
            record->trace[i].wakeup_cause = legacy.trace[i].wakeup_cause;
            record->trace[i].type = legacy.trace[i].type;
        }
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

static void power_experiment_default(power_experiment_record_t *record) {
    memset(record, 0, sizeof(*record));
    record->magic = POWER_EXPERIMENT_MAGIC;
    record->version = POWER_EXPERIMENT_VERSION;
    record->state = PLATFORM_POWER_EXPERIMENT_IDLE;
}

static bool power_experiment_load(power_experiment_record_t *record) {
    power_experiment_default(record);
    nvs_handle_t handle;
    if (nvs_open(POWER_EXPERIMENT_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(*record);
    power_experiment_record_t stored;
    const bool valid =
        nvs_get_blob(handle, POWER_EXPERIMENT_KEY, &stored, &size) == ESP_OK &&
        size == sizeof(stored) && stored.magic == POWER_EXPERIMENT_MAGIC &&
        stored.version == POWER_EXPERIMENT_VERSION &&
        stored.state <= PLATFORM_POWER_EXPERIMENT_COMPLETE &&
        stored.sample_count <= PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES &&
        stored.sample_next < PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES;
    nvs_close(handle);
    if (valid) {
        *record = stored;
    }
    return valid;
}

static bool power_experiment_save(const power_experiment_record_t *record) {
    nvs_handle_t handle;
    if (nvs_open(POWER_EXPERIMENT_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const esp_err_t set = nvs_set_blob(
        handle, POWER_EXPERIMENT_KEY, record, sizeof(*record));
    const esp_err_t commit = set == ESP_OK ? nvs_commit(handle) : set;
    nvs_close(handle);
    return set == ESP_OK && commit == ESP_OK;
}

static void power_experiment_append(
    power_experiment_record_t *record,
    const platform_power_measurement_t *measurement) {
    const uint8_t index =
        record->sample_next % PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES;
    platform_power_experiment_sample_t *sample = &record->samples[index];
    memset(sample, 0, sizeof(*sample));
    sample->sequence = ++record->next_sample_sequence;
    sample->elapsed_sec = record->elapsed_sec;
    sample->unix_time_ms = platform_utc_now_ms();
    sample->raw_adc = measurement && measurement->valid
        ? measurement->raw_adc : -1;
    sample->adc_mv = measurement && measurement->valid
        ? measurement->adc_mv : -1;
    sample->battery_mv = measurement && measurement->valid
        ? measurement->battery_mv : -1;
    sample->battery_level = measurement && measurement->valid
        ? measurement->battery_level : -1;
    sample->reset_reason = (int8_t)esp_reset_reason();
    sample->wakeup_cause = (int8_t)esp_sleep_get_wakeup_cause();
    record->sample_next =
        (uint8_t)((index + 1) % PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES);
    if (record->sample_count < PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES) {
        record->sample_count++;
    }
    record->total_samples++;
}

static uint32_t power_experiment_next_interval(
    const power_experiment_record_t *record) {
    const uint32_t remaining = record->maximum_duration_sec > record->elapsed_sec
        ? record->maximum_duration_sec - record->elapsed_sec : 0;
    if (remaining == 0) return 0;
    if (record->interval_sec == 0) return remaining;
    return record->interval_sec < remaining ? record->interval_sec : remaining;
}

bool platform_power_experiment_arm(uint32_t interval_sec,
                                   uint32_t maximum_duration_sec,
                                   uint64_t *experiment_id_out) {
    if (!platform_power_experiment_supported() || maximum_duration_sec == 0) {
        return false;
    }
    power_experiment_record_t record;
    power_experiment_default(&record);
    record.experiment_id = ((uint64_t)esp_random() << 32) | esp_random();
    if (record.experiment_id == 0) record.experiment_id = 1;
    record.state = PLATFORM_POWER_EXPERIMENT_ARMED;
    record.interval_sec = interval_sec;
    record.maximum_duration_sec = maximum_duration_sec;
    record.observer_effect = interval_sec > 0;
    if (!power_experiment_save(&record)) return false;
    if (experiment_id_out) *experiment_id_out = record.experiment_id;
    return true;
}

bool platform_power_experiment_note_entry(
    const platform_power_measurement_t *measurement,
    uint32_t *next_sleep_sec_out) {
    power_experiment_record_t record;
    if (!power_experiment_load(&record) ||
        record.state != PLATFORM_POWER_EXPERIMENT_ARMED) {
        return false;
    }
    record.state = PLATFORM_POWER_EXPERIMENT_RUNNING;
    record.elapsed_sec = 0;
    power_experiment_append(&record, measurement);
    record.next_sleep_sec = power_experiment_next_interval(&record);
    if (record.next_sleep_sec == 0 || !power_experiment_save(&record)) {
        return false;
    }
    if (next_sleep_sec_out) *next_sleep_sec_out = record.next_sleep_sec;
    return true;
}

bool platform_power_experiment_note_early_boot(
    const platform_power_measurement_t *measurement,
    uint32_t *next_sleep_sec_out) {
    power_experiment_record_t record;
    if (!power_experiment_load(&record) ||
        record.state != PLATFORM_POWER_EXPERIMENT_RUNNING) {
        return false;
    }
    const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
    if (wake == ESP_SLEEP_WAKEUP_TIMER) {
        record.elapsed_sec += record.next_sleep_sec;
        if (record.interval_sec > 0) record.intrusive_wakes++;
    }
    power_experiment_append(&record, measurement);

    const bool continue_sampling =
        wake == ESP_SLEEP_WAKEUP_TIMER && record.interval_sec > 0 &&
        record.elapsed_sec < record.maximum_duration_sec;
    if (continue_sampling) {
        record.next_sleep_sec = power_experiment_next_interval(&record);
        if (record.next_sleep_sec > 0 && power_experiment_save(&record)) {
            if (next_sleep_sec_out) *next_sleep_sec_out = record.next_sleep_sec;
            return true;
        }
    }

    record.state = PLATFORM_POWER_EXPERIMENT_COMPLETE;
    record.next_sleep_sec = 0;
    if (!power_experiment_save(&record)) {
        LOGE("Could not persist completed power experiment");
    }
    return false;
}

bool platform_power_experiment_snapshot(platform_power_experiment_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    power_experiment_record_t record;
    if (!power_experiment_load(&record)) return true;
    out->experiment_id = record.experiment_id;
    out->state = (platform_power_experiment_state_t)record.state;
    out->interval_sec = record.interval_sec;
    out->maximum_duration_sec = record.maximum_duration_sec;
    out->elapsed_sec = record.elapsed_sec;
    out->next_sleep_sec = record.next_sleep_sec;
    out->total_samples = record.total_samples;
    out->intrusive_wakes = record.intrusive_wakes;
    out->observer_effect = record.observer_effect != 0;
    out->sample_count = record.sample_count;
    const uint8_t start =
        record.sample_count == PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES
            ? record.sample_next : 0;
    for (uint8_t i = 0; i < record.sample_count; ++i) {
        out->samples[i] = record.samples[
            (start + i) % PLATFORM_POWER_EXPERIMENT_MAX_SAMPLES];
    }
    return true;
}

bool platform_power_experiment_clear(void) {
    nvs_handle_t handle;
    if (nvs_open(POWER_EXPERIMENT_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_erase_key(handle, POWER_EXPERIMENT_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
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
    event->unix_time_ms = platform_utc_now_ms();
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

void platform_power_evidence_note_time_sync(int64_t unix_time_ms,
                                            uint64_t uptime_ms) {
    if (unix_time_ms <= 0 || !power_evidence_lock()) return;
    power_evidence_record_t record;
    power_evidence_load(&record);
    bool changed = false;
    for (uint8_t i = 0; i < record.trace_count; ++i) {
        platform_power_trace_event_t *event = &record.trace[i];
        if (event->boot_id != record.boots || event->unix_time_ms != 0 ||
            uptime_ms < event->uptime_ms) {
            continue;
        }
        const uint64_t elapsed = uptime_ms - event->uptime_ms;
        if (elapsed <= (uint64_t)unix_time_ms) {
            event->unix_time_ms = unix_time_ms - (int64_t)elapsed;
            changed = true;
        }
    }
    if (changed && !power_evidence_save(&record)) {
        LOGE("Could not persist synchronized power evidence timestamps");
    }
    power_evidence_unlock();
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
void platform_power_evidence_note_time_sync(int64_t unix_time_ms,
                                            uint64_t uptime_ms) {
    (void)unix_time_ms;
    (void)uptime_ms;
}
bool platform_power_experiment_arm(uint32_t interval_sec,
                                   uint32_t maximum_duration_sec,
                                   uint64_t *experiment_id_out) {
    (void)interval_sec;
    (void)maximum_duration_sec;
    (void)experiment_id_out;
    return false;
}
bool platform_power_experiment_note_entry(
    const platform_power_measurement_t *measurement,
    uint32_t *next_sleep_sec_out) {
    (void)measurement;
    (void)next_sleep_sec_out;
    return false;
}
bool platform_power_experiment_note_early_boot(
    const platform_power_measurement_t *measurement,
    uint32_t *next_sleep_sec_out) {
    (void)measurement;
    (void)next_sleep_sec_out;
    return false;
}
bool platform_power_experiment_snapshot(platform_power_experiment_t *out) {
    if (out) memset(out, 0, sizeof(*out));
    return out != NULL;
}
bool platform_power_experiment_clear(void) { return false; }
#endif

PLATFORM_WEAK void platform_power_diagnostics_enrich(
    platform_power_diagnostics_t *out) {
    (void)out;
}

PLATFORM_WEAK bool platform_power_debug_arm_sleep(uint32_t delay_sec) {
    (void)delay_sec;
    return false;
}

PLATFORM_WEAK bool platform_power_experiment_supported(void) {
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
