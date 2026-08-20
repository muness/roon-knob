#include "power_debug_web.h"

#include "platform/platform_identity.h"
#include "platform/platform_log.h"
#include "platform/platform_power.h"

#include <esp_sleep.h>
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POWER_DEBUG_BODY_SIZE 6144
#define POWER_DEBUG_TEST_DELAY_SEC 15

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
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Out of memory");
        return ESP_FAIL;
    }

    const char *device = platform_device_slug();
    const bool evidence_supported =
        (power.capabilities & PLATFORM_POWER_CAP_DURABLE_EVIDENCE) != 0;
    const bool test_supported =
        (power.capabilities & PLATFORM_POWER_CAP_FORCED_TEST) != 0;

    if (wants_json) {
        httpd_resp_set_type(req, "application/json");
        snprintf(
            body, POWER_DEBUG_BODY_SIZE,
            "{\"schema_version\":1,\"device\":\"%s\","
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
            "\"reset_reason\":\"%s\",\"wakeup_cause\":\"%s\"},"
            "\"powered_test_supported\":%s}",
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
            reset_reason_name(power.reset_reason),
            wakeup_cause_name(power.wakeup_cause),
            test_supported ? "true" : "false");
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
            "<tr><th>This boot</th><td>reset %s · wake %s · uptime %llums</td></tr></table>"
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
            reset_reason_name(power.reset_reason),
            wakeup_cause_name(power.wakeup_cause),
            (unsigned long long)power.uptime_ms,
            test_supported
                ? "<h2>Powered test</h2><p>This bypasses the external-power timeout once. "
                  "The target enters its implemented sleep path after 15 seconds. Wake it with "
                  "its normal hardware control, then reload this page.</p><form method='POST' "
                  "action='/power-debug/sleep'><button type='submit'>Arm one-time 15-second "
                  "power-off test</button></form>"
                : "<h2>Powered test</h2><p>Unsupported on this target: firmware will not "
                  "claim a forced whole-device sleep path that has not been implemented.</p>");
    }

    httpd_resp_send(req, body, strlen(body));
    free(body);
    return ESP_OK;
}

static esp_err_t power_debug_sleep_handler(httpd_req_t *req) {
    if (!platform_power_debug_arm_sleep(POWER_DEBUG_TEST_DELAY_SEC)) {
        httpd_resp_set_status(req, "501 Not Implemented");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req,
                           "A forced powered sleep test is unsupported on this target");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(
        req,
        "<!DOCTYPE html><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Power test armed</title><h1>Power test armed</h1>"
        "<p>The implemented power-off path starts in 15 seconds. Use the target's normal "
        "hardware wake control, then reopen <a href='/power-debug'>power debug</a>.</p>");
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
    esp_err_t err = httpd_register_uri_handler(server, &get);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(server, &sleep);
    }
    if (err != ESP_OK) {
        LOGE("Could not register shared power-debug routes: %d", (int)err);
        return false;
    }
    return true;
}
