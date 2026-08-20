#include "m5_terminal_power.h"

#include "m5_platform.h"
#include "platform/platform_input.h"
#include "platform/platform_time.h"
#include "wifi_manager.h"

#include <esp_log.h>
#include <nvs.h>

static const char *TAG = "m5_terminal";
static const char *NAMESPACE = "pwr_evidence";
static uint64_t s_force_at_ms;
static uint32_t s_debug_arms;
static uint32_t s_display_sleeps;
static uint32_t s_runtime_wakes;

enum {
    KEY_ATTEMPTS = 1,
    KEY_PREFLIGHTS = 2,
    KEY_ENTRIES = 3,
    KEY_FLAGS = 4,
    KEY_ERROR = 5,
};

static const char *key_name(uint8_t key) {
    switch (key) {
    case KEY_ATTEMPTS: return "attempts";
    case KEY_PREFLIGHTS: return "preflights";
    case KEY_ENTRIES: return "entries";
    case KEY_FLAGS: return "flags";
    case KEY_ERROR: return "error";
    default: return "unknown";
    }
}

static uint32_t read_value(uint8_t key) {
    nvs_handle_t handle;
    uint32_t value = 0;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        (void)nvs_get_u32(handle, key_name(key), &value);
        nvs_close(handle);
    }
    return value;
}

static void write_value(uint8_t key, uint32_t value) {
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_u32(handle, key_name(key), value) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static void increment(uint8_t key) {
    write_value(key, read_value(key) + 1);
}

bool m5_terminal_power_debug_arm(uint32_t delay_sec) {
    if (delay_sec < 5 || delay_sec > 300) return false;
    s_force_at_ms = platform_millis() + (uint64_t)delay_sec * 1000ULL;
    ++s_debug_arms;
    ESP_LOGW(TAG, "Forced terminal-power test armed for %us", (unsigned)delay_sec);
    return true;
}

bool m5_terminal_power_debug_due(void) {
    return s_force_at_ms != 0 && platform_millis() >= s_force_at_ms;
}

void m5_terminal_power_note_display_sleep(void) { ++s_display_sleeps; }
void m5_terminal_power_note_runtime_wake(void) { ++s_runtime_wakes; }

bool m5_terminal_power_off(void) {
    s_force_at_ms = 0;
    increment(KEY_ATTEMPTS);
    write_value(KEY_FLAGS, 0);
    write_value(KEY_ERROR, PLATFORM_POWER_PREFLIGHT_ERROR_NONE);
    if (!platform_power_prepare_for_deep_sleep()) {
        write_value(KEY_ERROR, PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_CONFIG);
        ESP_LOGE(TAG, "Terminal preflight rejected; remaining awake");
        return false;
    }
    uint32_t flags = PLATFORM_POWER_PREFLIGHT_WAKE_SOURCES_SANITIZED;
    platform_input_shutdown();
    wifi_mgr_stop();
    flags |= PLATFORM_POWER_PREFLIGHT_WIFI_OFF |
             PLATFORM_POWER_PREFLIGHT_OUTPUTS_SAFE |
             PLATFORM_POWER_PREFLIGHT_DISPLAY_SAFE;
    write_value(KEY_FLAGS, flags);
    increment(KEY_PREFLIGHTS);
    increment(KEY_ENTRIES);
    ESP_LOGI(TAG, "Terminal preflight complete flags=0x%02lx",
             (unsigned long)flags);
    m5_platform_power_off();
    return false;
}

void m5_terminal_power_diagnostics(platform_power_diagnostics_t *out) {
    if (!out) return;
    out->capabilities |= PLATFORM_POWER_CAP_DISPLAY_SLEEP |
        PLATFORM_POWER_CAP_BOARD_POWER_OFF | PLATFORM_POWER_CAP_FORCED_TEST |
        PLATFORM_POWER_CAP_DURABLE_EVIDENCE;
    out->display_sleep_transitions = s_display_sleeps;
    out->runtime_wakes = s_runtime_wakes;
    out->debug_sleep_arms = s_debug_arms;
    out->debug_sleep_override_armed = s_force_at_ms != 0;
    out->deep_sleep_timer_active = s_force_at_ms != 0;
    out->power_off_attempts = read_value(KEY_ATTEMPTS);
    out->preflight_completions = read_value(KEY_PREFLIGHTS);
    out->power_off_entries = read_value(KEY_ENTRIES);
    out->last_preflight_flags = read_value(KEY_FLAGS);
    out->last_preflight_error = read_value(KEY_ERROR);
}
