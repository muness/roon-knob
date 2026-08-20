#include "rlcd_power_manager.h"

#include "ble_hid_host_rlcd.h"
#include "captive_portal.h"
#include "controller_config.h"
#include "platform/platform_input.h"
#include "platform/platform_task.h"
#include "platform_input_rlcd.h"
#include "rlcd_display.h"
#include "rlcd_ui.h"
#include "wifi_manager.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <string.h>

#define RLCD_WAKE_GPIO GPIO_NUM_18
#define RLCD_RETRY_COOLDOWN_MS (60ULL * 1000ULL)
#define RLCD_POWER_DEBUG_RTC_MAGIC 0x52505752u /* RPWR */

typedef struct {
    uint32_t magic;
    uint32_t attempts;
    uint32_t preflight_completions;
    uint32_t entries;
    uint32_t hardware_wakes;
    uint32_t last_preflight_flags;
    uint32_t last_preflight_error;
    int reset_reason;
    int wakeup_cause;
} rlcd_power_debug_rtc_t;

RTC_DATA_ATTR static rlcd_power_debug_rtc_t s_debug = {
    .magic = RLCD_POWER_DEBUG_RTC_MAGIC,
};

static const char *TAG = "rlcd_power";
static bool s_ble_quiescing;
static uint64_t s_retry_not_before_ms;
static uint64_t s_debug_force_not_before_ms;
static uint32_t s_debug_arm_count;

static uint64_t now_ms(void) {
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static void retained_init(void) {
    if (s_debug.magic == RLCD_POWER_DEBUG_RTC_MAGIC) return;
    memset(&s_debug, 0, sizeof(s_debug));
    s_debug.magic = RLCD_POWER_DEBUG_RTC_MAGIC;
}

static bool config_allows_sleep(uint64_t current_ms) {
#if CONFIG_RK_RLCD_DEEP_SLEEP
    controller_config_snapshot_t config = {0};
    if (!controller_config_snapshot(&config) ||
        config.durability == CONTROLLER_CONFIG_DURABILITY_UNINITIALIZED ||
        config.durability == CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY ||
        !config.value.deep_sleep_battery_enabled ||
        config.value.deep_sleep_battery_timeout_sec == 0) {
        return false;
    }
    const uint64_t deadline = rlcd_input_last_activity_ms() +
        (uint64_t)config.value.deep_sleep_battery_timeout_sec * 1000ULL;
    return current_ms >= deadline;
#else
    (void)current_ms;
    return false;
#endif
}

static bool runtime_safe(bool runtime_transition_pending) {
    return !runtime_transition_pending && !wifi_mgr_is_ap_mode() &&
           !rlcd_ui_power_work_pending() && !platform_task_has_pending() &&
           rlcd_input_wake_button_released();
}

static bool sleep_requested(bool runtime_transition_pending) {
    const uint64_t current_ms = now_ms();
    const bool forced = s_debug_force_not_before_ms > 0 &&
                        current_ms >= s_debug_force_not_before_ms;
    return (forced || config_allows_sleep(current_ms)) &&
           runtime_safe(runtime_transition_pending);
}

static bool arm_wake(void) {
    if (!rlcd_input_wake_button_released() ||
        gpio_get_level(RLCD_WAKE_GPIO) == 0) {
        s_debug.last_preflight_error =
            PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_ACTIVE;
        return false;
    }
    if (!platform_power_prepare_for_deep_sleep() ||
        rtc_gpio_pullup_en(RLCD_WAKE_GPIO) != ESP_OK ||
        rtc_gpio_pulldown_dis(RLCD_WAKE_GPIO) != ESP_OK ||
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,
                            ESP_PD_OPTION_ON) != ESP_OK ||
        esp_sleep_enable_ext1_wakeup_io(1ULL << RLCD_WAKE_GPIO,
                                        ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        s_debug.last_preflight_error =
            PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_CONFIG;
        ESP_LOGE(TAG, "Could not configure KEY wake; staying awake");
        return false;
    }
    s_debug.last_preflight_flags |=
        PLATFORM_POWER_PREFLIGHT_WAKE_SOURCES_SANITIZED |
        PLATFORM_POWER_PREFLIGHT_WAKE_ARMED;
    return true;
}

static void cancel_attempt(void) {
    if (s_ble_quiescing) (void)ble_hid_host_rlcd_cancel_sleep();
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    s_ble_quiescing = false;
    s_retry_not_before_ms = now_ms() + RLCD_RETRY_COOLDOWN_MS;
}

void rlcd_power_manager_init(void) {
    retained_init();
    s_ble_quiescing = false;
    s_retry_not_before_ms = 0;
    s_debug_force_not_before_ms = 0;
    s_debug_arm_count = 0;
    const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
    s_debug.reset_reason = (int)esp_reset_reason();
    s_debug.wakeup_cause = (int)wake;
    if (wake == ESP_SLEEP_WAKEUP_EXT1) s_debug.hardware_wakes++;
#if CONFIG_RK_RLCD_DEEP_SLEEP
    ESP_LOGI(TAG, "RLCD Deep-sleep enabled; KEY wake armed at sleep entry");
#else
    ESP_LOGI(TAG, "RLCD Deep-sleep disabled by build profile");
#endif
}

void rlcd_power_manager_poll(bool runtime_transition_pending) {
    const uint64_t current_ms = now_ms();
    if (!sleep_requested(runtime_transition_pending)) {
        if (s_ble_quiescing) cancel_attempt();
        return;
    }
    if (current_ms < s_retry_not_before_ms) return;

    if (!s_ble_quiescing) {
        retained_init();
        s_debug.attempts++;
        s_debug.last_preflight_flags = 0;
        s_debug.last_preflight_error = PLATFORM_POWER_PREFLIGHT_ERROR_NONE;
        if (!arm_wake()) {
            s_retry_not_before_ms = current_ms + RLCD_RETRY_COOLDOWN_MS;
            return;
        }
        s_ble_quiescing = true;
    }

    rlcd_ble_sleep_status_t ble = ble_hid_host_rlcd_prepare_for_sleep();
    if (ble == RLCD_BLE_SLEEP_PENDING) return;
    if (ble == RLCD_BLE_SLEEP_FAILED) {
        s_debug.last_preflight_error = PLATFORM_POWER_PREFLIGHT_ERROR_BLE;
        ESP_LOGW(TAG, "BLE did not quiesce; staying awake");
        cancel_attempt();
        return;
    }
    s_debug.last_preflight_flags |= PLATFORM_POWER_PREFLIGHT_BLE_OFF;

    if (!sleep_requested(runtime_transition_pending)) {
        s_debug.last_preflight_error = PLATFORM_POWER_PREFLIGHT_ERROR_POLICY;
        cancel_attempt();
        return;
    }
    if (!rlcd_display_prepare_for_sleep()) {
        s_debug.last_preflight_error = PLATFORM_POWER_PREFLIGHT_ERROR_DISPLAY;
        cancel_attempt();
        return;
    }
    s_debug.last_preflight_flags |= PLATFORM_POWER_PREFLIGHT_DISPLAY_SAFE;

    captive_portal_stop();
    platform_input_shutdown();
    s_debug.last_preflight_flags |= PLATFORM_POWER_PREFLIGHT_OUTPUTS_SAFE;
    wifi_mgr_stop();
    s_debug.last_preflight_flags |= PLATFORM_POWER_PREFLIGHT_WIFI_OFF;
    s_debug.preflight_completions++;
    s_debug.entries++;
    s_debug_force_not_before_ms = 0;
    ESP_LOGI(TAG,
             "Deep-sleep preflight complete: BLE off, WiFi off, ST7305 asleep; KEY wake armed");
    ESP_LOGI(TAG, "Entering ESP32-S3 Deep-sleep now");
    esp_deep_sleep_start();
}

void rlcd_power_manager_debug_enrich(platform_power_diagnostics_t *out) {
    if (!out) return;
    retained_init();
#if CONFIG_RK_RLCD_DEEP_SLEEP
    out->capabilities = PLATFORM_POWER_CAP_DISPLAY_SLEEP |
        PLATFORM_POWER_CAP_SOC_DEEP_SLEEP |
        PLATFORM_POWER_CAP_RTC_EVIDENCE |
        PLATFORM_POWER_CAP_FORCED_TEST;
#endif
    out->deep_sleep_timer_active = s_debug_force_not_before_ms > 0;
    out->debug_sleep_override_armed = s_debug_force_not_before_ms > 0;
    out->debug_sleep_arms = s_debug_arm_count;
    out->power_off_attempts = s_debug.attempts;
    out->preflight_completions = s_debug.preflight_completions;
    out->power_off_entries = s_debug.entries;
    out->hardware_wakes = s_debug.hardware_wakes;
    out->last_preflight_flags = s_debug.last_preflight_flags;
    out->last_preflight_error = s_debug.last_preflight_error;
    out->reset_reason = s_debug.reset_reason;
    out->wakeup_cause = s_debug.wakeup_cause;
}

bool rlcd_power_manager_debug_arm(uint32_t delay_sec) {
#if CONFIG_RK_RLCD_DEEP_SLEEP
    if (delay_sec < 5 || delay_sec > 300) return false;
    s_debug_force_not_before_ms = now_ms() + (uint64_t)delay_sec * 1000ULL;
    s_debug_arm_count++;
    ESP_LOGI(TAG, "Power debug armed one-time Deep-sleep in %lu sec",
             (unsigned long)delay_sec);
    return true;
#else
    (void)delay_sec;
    return false;
#endif
}
