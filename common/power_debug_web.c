#include "power_debug_web.h"

#include "platform/platform_identity.h"
#include "platform/platform_log.h"
#include "platform/platform_power.h"

#include <esp_sleep.h>
#include <esp_system.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POWER_DEBUG_BODY_SIZE 12288
#define POWER_DEBUG_TRACE_SIZE 3072
#define POWER_DEBUG_TEST_DELAY_SEC 15
#define POWER_EXPERIMENT_DEFAULT_INTERVAL_SEC 1200
#define POWER_EXPERIMENT_DEFAULT_DURATION_SEC 21600
#define POWER_EXPERIMENT_MAX_DURATION_SEC 604800

static const char *state_name(platform_power_state_t state) {
    switch (state) {
    case PLATFORM_POWER_STATE_ACTIVE: return "active";
    case PLATFORM_POWER_STATE_ART: return "art";
    case PLATFORM_POWER_STATE_DIM: return "dim";
    case PLATFORM_POWER_STATE_DISPLAY_SLEEP: return "display-sleep";
    default: return "unknown";
    }
}

static const char *strategy_name(uint32_t capabilities) {
    if (capabilities & PLATFORM_POWER_CAP_SOC_DEEP_SLEEP) {
        return "ESP32 Deep-sleep";
    }
    if (capabilities & PLATFORM_POWER_CAP_BOARD_POWER_OFF) {
        return "board/PMIC power-off";
    }
    if (capabilities & PLATFORM_POWER_CAP_DISPLAY_SLEEP) {
        return "display sleep only";
    }
    return "connected idle only";
}

static const char *reset_reason_name(int reason) {
    switch ((esp_reset_reason_t)reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other/unknown";
    }
}

static const char *wakeup_cause_name(int cause) {
    switch ((esp_sleep_wakeup_cause_t)cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "not-deep-sleep";
    case ESP_SLEEP_WAKEUP_EXT0: return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1: return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "touch";
    case ESP_SLEEP_WAKEUP_ULP: return "ULP";
    default: return "other";
    }
}

static const char *preflight_error_name(uint32_t error) {
    switch ((platform_power_preflight_error_t)error) {
    case PLATFORM_POWER_PREFLIGHT_ERROR_NONE: return "none";
    case PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_CONFIG: return "wake configuration";
    case PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_ACTIVE: return "wake input already active";
    case PLATFORM_POWER_PREFLIGHT_ERROR_BLE: return "BLE quiesce";
    case PLATFORM_POWER_PREFLIGHT_ERROR_DISPLAY: return "display shutdown";
    case PLATFORM_POWER_PREFLIGHT_ERROR_OUTPUTS: return "output shutdown/hold";
    case PLATFORM_POWER_PREFLIGHT_ERROR_WIFI: return "Wi-Fi shutdown";
    case PLATFORM_POWER_PREFLIGHT_ERROR_POLICY: return "policy/safety gate";
    default: return "unknown";
    }
}

static const char *trace_type_name(uint8_t type) {
    switch ((platform_power_trace_type_t)type) {
    case PLATFORM_POWER_TRACE_BOOT: return "boot";
    case PLATFORM_POWER_TRACE_ATTEMPT: return "attempt";
    case PLATFORM_POWER_TRACE_ERROR: return "error";
    case PLATFORM_POWER_TRACE_PREFLIGHT: return "preflight";
    case PLATFORM_POWER_TRACE_ENTRY: return "entry";
    default: return "unknown";
    }
}

static const char *experiment_state_name(
    platform_power_experiment_state_t state) {
    switch (state) {
    case PLATFORM_POWER_EXPERIMENT_ARMED: return "armed";
    case PLATFORM_POWER_EXPERIMENT_RUNNING: return "running";
    case PLATFORM_POWER_EXPERIMENT_COMPLETE: return "complete";
    case PLATFORM_POWER_EXPERIMENT_IDLE:
    default: return "idle";
    }
}

static bool parse_u32(const char *value, uint32_t *out) {
    if (!value || !value[0] || !out) return false;
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)parsed;
    return true;
}

static bool format_utc_timestamp(int64_t unix_time_ms, char *out,
                                 size_t out_size) {
    if (unix_time_ms <= 0 || !out || out_size == 0) return false;
    const time_t seconds = (time_t)(unix_time_ms / 1000);
    struct tm utc = {0};
    if (!gmtime_r(&seconds, &utc)) return false;
    const int millis = (int)(unix_time_ms % 1000);
    return snprintf(out, out_size,
                    "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                    utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                    utc.tm_hour, utc.tm_min, utc.tm_sec, millis) > 0;
}

static void append_text(char *buffer, size_t capacity, size_t *used,
                        const char *format, ...) {
    if (*used >= capacity) return;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(
        buffer + *used, capacity - *used, format, args);
    va_end(args);
    if (written < 0) return;
    const size_t available = capacity - *used;
    *used += (size_t)written >= available ? available - 1 : (size_t)written;
}

static void render_trace(const platform_power_diagnostics_t *power,
                         bool json, char *buffer, size_t capacity) {
    size_t used = 0;
    append_text(buffer, capacity, &used,
                json ? "[" : "<table><tr><th>#</th><th>Event</th><th>UTC / boot / uptime</th>"
                                  "<th>Battery</th><th>Flags / error</th>"
                                  "<th>Reset / wake</th></tr>");
    for (uint8_t i = 0; i < power->trace_event_count; ++i) {
        const platform_power_trace_event_t *event = &power->trace_events[i];
        char timestamp[32] = {0};
        const bool timestamp_valid = format_utc_timestamp(
            event->unix_time_ms, timestamp, sizeof(timestamp));
        if (json) {
            append_text(
                buffer, capacity, &used,
                "%s{\"sequence\":%lu,\"type\":\"%s\",\"boot_id\":%lu,"
                "\"uptime_ms\":%lu,\"unix_time_ms\":%lld,"
                "\"timestamp_utc\":%s%s%s,\"battery_level\":%d,\"flags\":%lu,"
                "\"error\":\"%s\",\"reset_reason\":\"%s\","
                "\"wakeup_cause\":\"%s\"}",
                i ? "," : "", (unsigned long)event->sequence,
                trace_type_name(event->type), (unsigned long)event->boot_id,
                (unsigned long)event->uptime_ms,
                (long long)event->unix_time_ms,
                timestamp_valid ? "\"" : "", timestamp_valid ? timestamp : "null",
                timestamp_valid ? "\"" : "", event->battery_level,
                (unsigned long)event->preflight_flags,
                preflight_error_name(event->preflight_error),
                reset_reason_name(event->reset_reason),
                wakeup_cause_name(event->wakeup_cause));
        } else {
            append_text(
                buffer, capacity, &used,
                "<tr><td>%lu</td><td>%s</td><td>%s<br>%lu / %lums</td><td>%d</td>"
                "<td><code>0x%02lx</code> / %s</td><td>%s / %s</td></tr>",
                (unsigned long)event->sequence, trace_type_name(event->type),
                timestamp_valid ? timestamp : "clock not synchronized",
                (unsigned long)event->boot_id,
                (unsigned long)event->uptime_ms, event->battery_level,
                (unsigned long)event->preflight_flags,
                preflight_error_name(event->preflight_error),
                reset_reason_name(event->reset_reason),
                wakeup_cause_name(event->wakeup_cause));
        }
    }
    append_text(buffer, capacity, &used, json ? "]" : "</table>");
}

static esp_err_t power_debug_get_handler(httpd_req_t *req) {
    platform_power_diagnostics_t power = {0};
    platform_power_diagnostics_snapshot(&power);

    char query[32] = {0};
    char format[8] = {0};
    const bool wants_json =
        httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "format", format, sizeof(format)) ==
            ESP_OK &&
        strcmp(format, "json") == 0;

    char *body = malloc(POWER_DEBUG_BODY_SIZE);
    char *trace = malloc(POWER_DEBUG_TRACE_SIZE);
    if (!body || !trace) {
        free(body);
        free(trace);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Out of memory");
        return ESP_FAIL;
    }
    render_trace(&power, wants_json, trace, POWER_DEBUG_TRACE_SIZE);

    const char *device = platform_device_slug();
    const bool evidence_supported =
        (power.capabilities & PLATFORM_POWER_CAP_DURABLE_EVIDENCE) != 0;
    const bool experiment_supported =
        (power.capabilities & PLATFORM_POWER_CAP_VOLTAGE_EXPERIMENT) != 0;
    platform_power_experiment_t experiment = {0};
    (void)platform_power_experiment_snapshot(&experiment);

    if (wants_json) {
        httpd_resp_set_type(req, "application/json");
        snprintf(
            body, POWER_DEBUG_BODY_SIZE,
            "{\"schema_version\":3,\"device\":\"%s\","
            "\"measurement_scope\":\"firmware evidence; not an ammeter\","
            "\"uptime_ms\":%llu,\"state\":\"%s\",\"strategy\":\"%s\","
            "\"capabilities\":%lu,\"battery_level\":%d,"
            "\"observed_source\":\"%s\",\"external_power\":%s,"
            "\"external_power_policy\":%s,"
            "\"policy\":{\"known\":%s,\"art_sec\":%lu,\"dim_sec\":%lu,"
            "\"display_sleep_sec\":%lu,\"power_off_sec\":%lu},"
            "\"runtime\":{\"wifi_modem_sleep_baseline\":%s,"
            "\"automatic_light_sleep_configured\":%s,"
            "\"deep_sleep_timer_active\":%s,\"debug_override_armed\":%s},"
            "\"current_boot_transitions\":{\"art\":%lu,\"dim\":%lu,"
            "\"display_sleep\":%lu,\"wake\":%lu,\"debug_sleep_arms\":%lu},"
            "\"retained_evidence\":{\"supported\":%s,\"attempts\":%lu,"
            "\"preflight_completions\":%lu,\"entries\":%lu,\"hardware_wakes\":%lu,"
            "\"last_preflight_flags\":%lu,\"last_preflight_error\":\"%s\","
            "\"durable_boots\":%lu,\"brownout_boots\":%lu,"
            "\"entry_pending\":%s,\"last_boot_followed_entry\":%s,"
            "\"last_entry_battery_level\":%d,"
            "\"reset_reason\":\"%s\",\"wakeup_cause\":\"%s\","
            "\"events\":%s},"
            "\"power_experiment_supported\":%s,"
            "\"experiment\":{\"id\":\"%016llx\",\"state\":\"%s\","
            "\"interval_sec\":%lu,\"maximum_duration_sec\":%lu,"
            "\"elapsed_sec\":%lu,\"samples\":%lu,\"retained_samples\":%u,"
            "\"samples_truncated\":%s,"
            "\"intrusive_wakes\":%lu,\"observer_effect\":%s}}",
            device, (unsigned long long)power.uptime_ms,
            state_name(power.state), strategy_name(power.capabilities),
            (unsigned long)power.capabilities, power.power.battery_level,
            platform_power_source_name(power.power.source),
            power.power.external_power ? "true" : "false",
            power.power.external_power ? "true" : "false",
            power.policy_known ? "true" : "false",
            (unsigned long)power.art_timeout_sec,
            (unsigned long)power.dim_timeout_sec,
            (unsigned long)power.display_sleep_timeout_sec,
            (unsigned long)power.power_off_timeout_sec,
            power.wifi_modem_sleep_baseline ? "true" : "false",
            power.automatic_light_sleep_configured ? "true" : "false",
            power.deep_sleep_timer_active ? "true" : "false",
            power.debug_sleep_override_armed ? "true" : "false",
            (unsigned long)power.art_transitions,
            (unsigned long)power.dim_transitions,
            (unsigned long)power.display_sleep_transitions,
            (unsigned long)power.runtime_wakes,
            (unsigned long)power.debug_sleep_arms,
            evidence_supported ? "true" : "false",
            (unsigned long)power.power_off_attempts,
            (unsigned long)power.preflight_completions,
            (unsigned long)power.power_off_entries,
            (unsigned long)power.hardware_wakes,
            (unsigned long)power.last_preflight_flags,
            preflight_error_name(power.last_preflight_error),
            (unsigned long)power.durable_boots,
            (unsigned long)power.durable_brownouts,
            power.terminal_entry_pending ? "true" : "false",
            power.last_boot_followed_terminal_entry ? "true" : "false",
            power.last_entry_battery_level,
            reset_reason_name(power.reset_reason),
            wakeup_cause_name(power.wakeup_cause),
            trace,
            experiment_supported ? "true" : "false",
            (unsigned long long)experiment.experiment_id,
            experiment_state_name(experiment.state),
            (unsigned long)experiment.interval_sec,
            (unsigned long)experiment.maximum_duration_sec,
            (unsigned long)experiment.elapsed_sec,
            (unsigned long)experiment.total_samples,
            experiment.sample_count,
            experiment.total_samples > experiment.sample_count ? "true" : "false",
            (unsigned long)experiment.intrusive_wakes,
            experiment.observer_effect ? "true" : "false");
    } else {
        httpd_resp_set_type(req, "text/html");
        snprintf(
            body, POWER_DEBUG_BODY_SIZE,
            "<!DOCTYPE html><html><head><meta name='viewport' "
            "content='width=device-width,initial-scale=1'><title>%s power debug</title>"
            "<style>body{font-family:sans-serif;margin:20px;background:#1a1a2e;color:#eee}"
            "h1,h2{color:#4fc3f7}table{border-collapse:collapse;max-width:720px;width:100%%}"
            "td,th{border-bottom:1px solid #444;padding:7px;text-align:left}code{color:#b3e5fc}"
            "button{padding:12px;background:#ffb300;border:0;border-radius:5px;font-weight:bold}"
            "a{color:#4fc3f7}</style></head><body><h1>%s power debug</h1>"
            "<p>Firmware evidence only; this page does not measure current%s.</p>"
            "<p><a href='/'>Device UI</a> · <a href='/power-debug?format=json'>JSON</a></p>"
            "<h2>Current policy</h2><table><tr><th>State</th><td>%s</td></tr>"
            "<tr><th>Power strategy</th><td>%s</td></tr>"
            "<tr><th>Observed source</th><td>%s</td></tr>"
            "<tr><th>Effective policy</th><td>%s</td></tr>"
            "<tr><th>Battery</th><td>%d</td></tr>"
            "<tr><th>Timeouts</th><td>art %lus · dim %lus · display %lus · power-off %lus</td></tr>"
            "<tr><th>Wi-Fi modem-sleep baseline</th><td>%s</td></tr>"
            "<tr><th>Automatic Light-sleep configured</th><td>%s</td></tr>"
            "<tr><th>Power-off timer active</th><td>%s%s</td></tr></table>"
            "<h2>Current boot transitions</h2><p>art %lu · dim %lu · display sleep %lu · "
            "wake %lu · debug arms %lu</p><h2>Retained power-off evidence</h2>"
            "<p>%s</p><table><tr><th>Attempts</th><td>%lu</td></tr>"
            "<tr><th>Completed preflights</th><td>%lu</td></tr>"
            "<tr><th>Entries requested</th><td>%lu</td></tr>"
            "<tr><th>Hardware wakes</th><td>%lu</td></tr>"
            "<tr><th>Last preflight flags</th><td><code>0x%02lx</code></td></tr>"
            "<tr><th>Last preflight error</th><td>%s</td></tr>"
            "<tr><th>Durable boots / brownouts</th><td>%lu / %lu</td></tr>"
            "<tr><th>Pending entry</th><td>%s</td></tr>"
            "<tr><th>Last boot followed an entry</th><td>%s</td></tr>"
            "<tr><th>Entry battery</th><td>%d</td></tr>"
            "<tr><th>This boot</th><td>reset %s · wake %s · uptime %llums</td></tr></table>"
            "<h2>Persistent event tail</h2>%s"
            "%s</body></html>",
            device, device,
            (power.capabilities & PLATFORM_POWER_CAP_AUXILIARY_SOC)
                ? " or prove the auxiliary processor's draw" : "",
            state_name(power.state), strategy_name(power.capabilities),
            platform_power_source_name(power.power.source),
            power.power.external_power ? "external/charging" : "battery",
            power.power.battery_level,
            (unsigned long)power.art_timeout_sec,
            (unsigned long)power.dim_timeout_sec,
            (unsigned long)power.display_sleep_timeout_sec,
            (unsigned long)power.power_off_timeout_sec,
            power.wifi_modem_sleep_baseline ? "yes" : "no",
            power.automatic_light_sleep_configured ? "yes" : "no",
            power.deep_sleep_timer_active ? "yes" : "no",
            power.debug_sleep_override_armed ? " (debug override)" : "",
            (unsigned long)power.art_transitions,
            (unsigned long)power.dim_transitions,
            (unsigned long)power.display_sleep_transitions,
            (unsigned long)power.runtime_wakes,
            (unsigned long)power.debug_sleep_arms,
            evidence_supported
                ? "Counters survive this target's implemented terminal power cycle."
                : "This target does not currently expose retained terminal-power evidence.",
            (unsigned long)power.power_off_attempts,
            (unsigned long)power.preflight_completions,
            (unsigned long)power.power_off_entries,
            (unsigned long)power.hardware_wakes,
            (unsigned long)power.last_preflight_flags,
            preflight_error_name(power.last_preflight_error),
            (unsigned long)power.durable_boots,
            (unsigned long)power.durable_brownouts,
            power.terminal_entry_pending ? "yes" : "no",
            power.last_boot_followed_terminal_entry ? "yes" : "no",
            power.last_entry_battery_level,
            reset_reason_name(power.reset_reason),
            wakeup_cause_name(power.wakeup_cause),
            (unsigned long long)power.uptime_ms,
            trace,
            experiment_supported
                ? "<h2>Power experiment</h2><p>The software-only mode periodically wakes "
                  "just far enough to persist raw battery samples, then returns to sleep "
                  "without starting the display, Wi-Fi, BLE, or application. These wakes "
                  "alter the measured drain. Set the interval to 0 when an external current "
                  "profiler is recording.</p><p><a href='/power-debug/sleep'>View/export the "
                  "experiment record</a></p><form method='POST' action='/power-debug/sleep'>"
                  "<label>Sample interval seconds (0 = external profiler) "
                  "<input name='interval_sec' value='1200'></label><br><label>Maximum duration "
                  "seconds <input name='duration_sec' value='21600'></label><br>"
                  "<button type='submit'>Arm experiment; sleep in 15 seconds</button></form>"
                  "<form method='POST' action='/power-debug/sleep?action=clear'>"
                  "<button type='submit'>Clear experiment record</button></form>"
                : "<h2>Power experiment</h2><p>Unsupported on this exact target: it does "
                  "not yet implement the early, radio/UI-free voltage-sampling path.</p>");
    }

    httpd_resp_send(req, body, strlen(body));
    free(body);
    free(trace);
    return ESP_OK;
}

static void render_experiment_json(const platform_power_experiment_t *experiment,
                                   char *body, size_t capacity) {
    size_t used = 0;
    append_text(body, capacity, &used,
                "{\"schema_version\":1,\"experiment_id\":\"%016llx\","
                "\"state\":\"%s\",\"sampling_mode\":\"%s\","
                "\"measurement_scope\":\"battery voltage evidence; not an ammeter\","
                "\"interval_sec\":%lu,\"maximum_duration_sec\":%lu,"
                "\"elapsed_sec\":%lu,\"total_samples\":%lu,"
                "\"retained_samples\":%u,\"samples_truncated\":%s,"
                "\"intrusive_wakes\":%lu,\"observer_effect\":%s,\"samples\":[",
                (unsigned long long)experiment->experiment_id,
                experiment_state_name(experiment->state),
                experiment->interval_sec ? "software-checkpoint" : "external-profiler",
                (unsigned long)experiment->interval_sec,
                (unsigned long)experiment->maximum_duration_sec,
                (unsigned long)experiment->elapsed_sec,
                (unsigned long)experiment->total_samples,
                experiment->sample_count,
                experiment->total_samples > experiment->sample_count ? "true" : "false",
                (unsigned long)experiment->intrusive_wakes,
                experiment->observer_effect ? "true" : "false");
    for (uint8_t i = 0; i < experiment->sample_count; ++i) {
        const platform_power_experiment_sample_t *sample = &experiment->samples[i];
        char timestamp[32] = {0};
        const bool timestamp_valid = format_utc_timestamp(
            sample->unix_time_ms, timestamp, sizeof(timestamp));
        append_text(body, capacity, &used,
                    "%s{\"sequence\":%lu,\"elapsed_sec\":%lu,"
                    "\"unix_time_ms\":%lld,\"timestamp_utc\":%s%s%s,"
                    "\"raw_adc\":%ld,\"adc_mv\":%ld,\"battery_mv\":%ld,"
                    "\"battery_level\":%d,\"reset_reason\":\"%s\","
                    "\"wakeup_cause\":\"%s\"}",
                    i ? "," : "", (unsigned long)sample->sequence,
                    (unsigned long)sample->elapsed_sec,
                    (long long)sample->unix_time_ms,
                    timestamp_valid ? "\"" : "",
                    timestamp_valid ? timestamp : "null",
                    timestamp_valid ? "\"" : "",
                    (long)sample->raw_adc, (long)sample->adc_mv,
                    (long)sample->battery_mv, sample->battery_level,
                    reset_reason_name(sample->reset_reason),
                    wakeup_cause_name(sample->wakeup_cause));
    }
    append_text(body, capacity, &used, "]}");
}

static esp_err_t power_debug_sleep_get_handler(httpd_req_t *req) {
    platform_power_experiment_t experiment = {0};
    (void)platform_power_experiment_snapshot(&experiment);
    char query[32] = {0};
    char format[8] = {0};
    const bool wants_csv =
        httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "format", format, sizeof(format)) == ESP_OK &&
        strcmp(format, "csv") == 0;
    char *body = malloc(POWER_DEBUG_BODY_SIZE);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    if (wants_csv) {
        size_t used = 0;
        append_text(body, POWER_DEBUG_BODY_SIZE, &used,
                    "experiment_id,sequence,elapsed_sec,unix_time_ms,raw_adc,adc_mv,"
                    "battery_mv,battery_level,reset_reason,wakeup_cause\n");
        for (uint8_t i = 0; i < experiment.sample_count; ++i) {
            const platform_power_experiment_sample_t *sample = &experiment.samples[i];
            append_text(body, POWER_DEBUG_BODY_SIZE, &used,
                        "%016llx,%lu,%lu,%lld,%ld,%ld,%ld,%d,%s,%s\n",
                        (unsigned long long)experiment.experiment_id,
                        (unsigned long)sample->sequence,
                        (unsigned long)sample->elapsed_sec,
                        (long long)sample->unix_time_ms,
                        (long)sample->raw_adc, (long)sample->adc_mv,
                        (long)sample->battery_mv, sample->battery_level,
                        reset_reason_name(sample->reset_reason),
                        wakeup_cause_name(sample->wakeup_cause));
        }
        httpd_resp_set_type(req, "text/csv");
    } else {
        render_experiment_json(&experiment, body, POWER_DEBUG_BODY_SIZE);
        httpd_resp_set_type(req, "application/json");
    }
    httpd_resp_send(req, body, strlen(body));
    free(body);
    return ESP_OK;
}

static esp_err_t power_debug_sleep_handler(httpd_req_t *req) {
    char params[128] = {0};
    char value[24] = {0};
    uint32_t interval_sec = POWER_EXPERIMENT_DEFAULT_INTERVAL_SEC;
    uint32_t duration_sec = POWER_EXPERIMENT_DEFAULT_DURATION_SEC;
    bool has_params =
        httpd_req_get_url_query_str(req, params, sizeof(params)) == ESP_OK;
    if (!has_params && req->content_len > 0) {
        if (req->content_len >= sizeof(params)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Request parameters are too long");
            return ESP_FAIL;
        }
        size_t received = 0;
        while (received < req->content_len) {
            const int chunk = httpd_req_recv(
                req, params + received, req->content_len - received);
            if (chunk <= 0) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                    "Could not read request parameters");
                return ESP_FAIL;
            }
            received += (size_t)chunk;
        }
        params[received] = '\0';
        has_params = true;
    }
    if (has_params &&
        httpd_query_key_value(params, "action", value, sizeof(value)) == ESP_OK &&
        strcmp(value, "clear") == 0) {
        if (!platform_power_experiment_clear()) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Could not clear experiment");
            return ESP_FAIL;
        }
        httpd_resp_sendstr(req, "Power experiment cleared");
        return ESP_OK;
    }
    if (has_params &&
        httpd_query_key_value(params, "interval_sec", value, sizeof(value)) == ESP_OK) {
        if (!parse_u32(value, &interval_sec)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "interval_sec must be an unsigned integer");
            return ESP_FAIL;
        }
    }
    if (has_params &&
        httpd_query_key_value(params, "duration_sec", value, sizeof(value)) == ESP_OK) {
        if (!parse_u32(value, &duration_sec)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "duration_sec must be an unsigned integer");
            return ESP_FAIL;
        }
    }
    if ((interval_sec > 0 && interval_sec < 30) || interval_sec > 3600 ||
        duration_sec == 0 || duration_sec > POWER_EXPERIMENT_MAX_DURATION_SEC ||
        (interval_sec > 0 && duration_sec < interval_sec)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "interval_sec must be 0 or 30..3600; duration_sec must be "
                            "at least one interval and no more than 604800");
        return ESP_FAIL;
    }
    uint64_t experiment_id = 0;
    if (!platform_power_experiment_arm(
            interval_sec, duration_sec, &experiment_id)) {
        httpd_resp_set_status(req, "501 Not Implemented");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req,
                           "A persistent voltage experiment is unsupported on this target");
        return ESP_FAIL;
    }
    if (!platform_power_debug_arm_sleep(POWER_DEBUG_TEST_DELAY_SEC)) {
        (void)platform_power_experiment_clear();
        httpd_resp_set_status(req, "501 Not Implemented");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req,
                           "A forced powered sleep test is unsupported on this target");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "text/html");
    char response[640];
    snprintf(response, sizeof(response),
             "<!DOCTYPE html><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Power experiment armed</title><h1>Power experiment armed</h1>"
             "<p>Experiment <code>%016llx</code> starts its power-off path in 15 seconds. "
             "Results persist at <a href='/power-debug/sleep'>power experiment</a>. "
             "Software checkpoints change the measured drain; interval 0 is reserved "
             "for external-profiler runs.</p>",
             (unsigned long long)experiment_id);
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

bool power_debug_web_register(httpd_handle_t server) {
    if (!server) {
        return false;
    }
    const httpd_uri_t get = {
        .uri = "/power-debug",
        .method = HTTP_GET,
        .handler = power_debug_get_handler,
    };
    const httpd_uri_t sleep = {
        .uri = "/power-debug/sleep",
        .method = HTTP_POST,
        .handler = power_debug_sleep_handler,
    };
    const httpd_uri_t sleep_get = {
        .uri = "/power-debug/sleep",
        .method = HTTP_GET,
        .handler = power_debug_sleep_get_handler,
    };
    esp_err_t err = httpd_register_uri_handler(server, &get);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(server, &sleep_get);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(server, &sleep);
    }
    if (err != ESP_OK) {
        LOGE("Could not register shared power-debug routes: %d", (int)err);
        return false;
    }
    return true;
}
