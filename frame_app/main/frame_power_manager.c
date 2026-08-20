#include "frame_power_manager.h"

#include "ble_hid_host_frame.h"
#include "bridge_client.h"
#include "captive_portal.h"
#include "controller_config.h"
#include "eink_ui.h"
#include "frame_power_policy.h"
#include "platform/platform_input.h"
#include "platform/platform_task.h"
#include "platform_input_frame.h"
#include "pmic_axp2101.h"
#include "wifi_manager.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <string.h>

static const char *TAG = "frame_power";

#define FRAME_WAKE_GPIO GPIO_NUM_4
#define FRAME_TIMER_WAKE_US (30ULL * 60ULL * 1000000ULL)
#define FRAME_TIMER_WAKE_GRACE_MS (60ULL * 1000ULL)
#define FRAME_RETRY_COOLDOWN_MS (60ULL * 1000ULL)
#define FRAME_SOURCE_CACHE_MS 1000ULL
#define FRAME_POWER_DEBUG_RTC_MAGIC 0x46505752u  // "FPWR"

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
} frame_power_debug_rtc_t;

RTC_DATA_ATTR static frame_power_debug_rtc_t s_power_debug_rtc = {
    .magic = FRAME_POWER_DEBUG_RTC_MAGIC,
};

typedef enum {
    FRAME_SLEEP_IDLE = 0,
    FRAME_SLEEP_QUIESCING_BLE,
} frame_sleep_state_t;

static frame_sleep_state_t s_state;
static uint64_t s_boot_ms;
static uint64_t s_initial_input_ms;
static uint64_t s_retry_not_before_ms;
static bool s_timer_wake;
static bool s_have_cached_source;
static uint64_t s_source_checked_ms;
static frame_power_source_t s_cached_source;
static frame_power_decision_t s_last_decision = FRAME_POWER_READY;
static uint64_t s_debug_force_not_before_ms;
static uint32_t s_debug_arm_count;

static void power_debug_rtc_init(void) {
    if (s_power_debug_rtc.magic == FRAME_POWER_DEBUG_RTC_MAGIC) {
        return;
    }
    memset(&s_power_debug_rtc, 0, sizeof(s_power_debug_rtc));
    s_power_debug_rtc.magic = FRAME_POWER_DEBUG_RTC_MAGIC;
}

static uint64_t now_ms(void) {
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static frame_power_source_t power_source(bool force) {
    const uint64_t current_ms = now_ms();
    if (!force && s_have_cached_source &&
        current_ms - s_source_checked_ms < FRAME_SOURCE_CACHE_MS) {
        return s_cached_source;
    }
    switch (pmic_power_source()) {
    case PMIC_POWER_SOURCE_BATTERY:
        s_cached_source = FRAME_POWER_SOURCE_BATTERY;
        break;
    case PMIC_POWER_SOURCE_EXTERNAL:
        s_cached_source = FRAME_POWER_SOURCE_EXTERNAL;
        break;
    case PMIC_POWER_SOURCE_UNKNOWN:
    default:
        s_cached_source = FRAME_POWER_SOURCE_UNKNOWN;
        break;
    }
    s_have_cached_source = true;
    s_source_checked_ms = current_ms;
    return s_cached_source;
}

static const char *decision_name(frame_power_decision_t decision) {
    switch (decision) {
    case FRAME_POWER_READY: return "ready";
    case FRAME_POWER_BLOCK_DISABLED: return "disabled";
    case FRAME_POWER_BLOCK_SOURCE_UNKNOWN: return "power-source-unknown";
    case FRAME_POWER_BLOCK_EXTERNAL_POWER: return "external-power";
    case FRAME_POWER_BLOCK_TIMEOUT: return "idle-timeout";
    case FRAME_POWER_BLOCK_BRIDGE: return "bridge-not-connected";
    case FRAME_POWER_BLOCK_ZONE_UNKNOWN: return "zone-state-unknown";
    case FRAME_POWER_BLOCK_PLAYING: return "playback-active";
    case FRAME_POWER_BLOCK_PROVISIONING: return "provisioning";
    case FRAME_POWER_BLOCK_UI_PENDING: return "eink-work-pending";
    case FRAME_POWER_BLOCK_TASK_PENDING: return "ui-callback-pending";
    case FRAME_POWER_BLOCK_RUNTIME_TRANSITION: return "runtime-transition";
    case FRAME_POWER_BLOCK_CONFIG_DURABILITY: return "config-not-durable";
    case FRAME_POWER_BLOCK_WAKE_BUTTON: return "wake-button-held";
    default: return "unknown";
    }
}

static bool configure_wake_sources(void) {
    if (!frame_input_wake_button_released() ||
        gpio_get_level(FRAME_WAKE_GPIO) == 0) {
        s_power_debug_rtc.last_preflight_error =
            PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_ACTIVE;
        return false;
    }
    if (!platform_power_prepare_for_deep_sleep() ||
        rtc_gpio_pullup_en(FRAME_WAKE_GPIO) != ESP_OK ||
        rtc_gpio_pulldown_dis(FRAME_WAKE_GPIO) != ESP_OK ||
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,
                            ESP_PD_OPTION_ON) != ESP_OK ||
        esp_sleep_enable_ext1_wakeup_io(1ULL << FRAME_WAKE_GPIO,
                                        ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK ||
        esp_sleep_enable_timer_wakeup(FRAME_TIMER_WAKE_US) != ESP_OK) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        s_power_debug_rtc.last_preflight_error =
            PLATFORM_POWER_PREFLIGHT_ERROR_WAKE_CONFIG;
        ESP_LOGE(TAG, "Could not configure KEY/timer wake; staying awake");
        return false;
    }
    s_power_debug_rtc.last_preflight_flags |=
        PLATFORM_POWER_PREFLIGHT_WAKE_SOURCES_SANITIZED |
        PLATFORM_POWER_PREFLIGHT_WAKE_ARMED;
    return true;
}

static frame_power_decision_t build_snapshot(
    bool runtime_transition_pending, bool force_source,
    frame_power_snapshot_t *snapshot) {
    controller_config_snapshot_t config = {0};
    const bool have_config = controller_config_snapshot(&config);
    const uint64_t current_ms = now_ms();
    const uint64_t last_input_ms = frame_input_last_activity_ms();
    uint64_t sleep_not_before_ms = UINT64_MAX;
    bool enabled = false;

#if CONFIG_RK_FRAME_DEEP_SLEEP
    if (have_config && config.value.deep_sleep_battery_enabled &&
        config.value.deep_sleep_battery_timeout_sec > 0) {
        enabled = true;
        sleep_not_before_ms =
            last_input_ms +
            (uint64_t)config.value.deep_sleep_battery_timeout_sec * 1000ULL;
        if (s_timer_wake && last_input_ms == s_initial_input_ms) {
            sleep_not_before_ms = s_boot_ms + FRAME_TIMER_WAKE_GRACE_MS;
        }
    }
#endif

    *snapshot = (frame_power_snapshot_t){
        .enabled = enabled,
        // Read the PMIC only after every cheaper inhibitor has cleared.
        .power_source = FRAME_POWER_SOURCE_UNKNOWN,
        .now_ms = current_ms,
        .sleep_not_before_ms = sleep_not_before_ms,
        .bridge_connected = bridge_client_is_bridge_connected(),
        .zone_state_known = eink_ui_power_state_known(),
        .playing = eink_ui_is_playing(),
        /* The same HTTP server serves connected settings in STA mode. Its
         * existence is not provisioning and must not permanently veto sleep. */
        .provisioning = wifi_mgr_is_ap_mode(),
        .ui_pending = eink_ui_has_pending_refresh(),
        .task_pending = platform_task_has_pending(),
        .runtime_transition_pending = runtime_transition_pending,
        .config_durable =
            have_config &&
            config.durability != CONTROLLER_CONFIG_DURABILITY_UNINITIALIZED &&
            config.durability != CONTROLLER_CONFIG_DURABILITY_VOLATILE_RECOVERY,
        .wake_button_released = frame_input_wake_button_released(),
    };
    frame_power_decision_t decision = frame_power_policy_decide(snapshot);
    const bool forced = s_debug_force_not_before_ms > 0 &&
        current_ms >= s_debug_force_not_before_ms;
    if (forced) {
        /* A debug request bypasses user-policy/playing/source gates, but still
         * honors provisioning, pending work, config durability, and the wake
         * button so it cannot enter through an unsafe teardown state. */
        snapshot->enabled = true;
        snapshot->power_source = FRAME_POWER_SOURCE_BATTERY;
        snapshot->sleep_not_before_ms = current_ms;
        snapshot->bridge_connected = true;
        snapshot->zone_state_known = true;
        snapshot->playing = false;
        decision = frame_power_policy_decide(snapshot);
    }
    if (decision == FRAME_POWER_BLOCK_SOURCE_UNKNOWN) {
        snapshot->power_source = power_source(force_source);
        decision = frame_power_policy_decide(snapshot);
    }
    return decision;
}

static bool cancel_sleep_attempt(void) {
    if (s_state == FRAME_SLEEP_QUIESCING_BLE) {
        if (!ble_hid_host_frame_cancel_sleep()) {
            ESP_LOGW(TAG, "BLE sleep cancel queue busy; will retry");
            return false;
        }
    }
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    s_state = FRAME_SLEEP_IDLE;
    s_retry_not_before_ms = now_ms() + FRAME_RETRY_COOLDOWN_MS;
    return true;
}

void frame_power_manager_init(void) {
    s_state = FRAME_SLEEP_IDLE;
    s_boot_ms = now_ms();
    s_initial_input_ms = frame_input_last_activity_ms();
    s_retry_not_before_ms = 0;
    s_timer_wake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    s_have_cached_source = false;
    s_source_checked_ms = 0;
    s_cached_source = FRAME_POWER_SOURCE_UNKNOWN;
    s_last_decision = FRAME_POWER_READY;
    s_debug_force_not_before_ms = 0;
    s_debug_arm_count = 0;
    power_debug_rtc_init();
    const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
    s_power_debug_rtc.reset_reason = (int)esp_reset_reason();
    s_power_debug_rtc.wakeup_cause = (int)wake;
    if (wake == ESP_SLEEP_WAKEUP_EXT1 || wake == ESP_SLEEP_WAKEUP_TIMER) {
        s_power_debug_rtc.hardware_wakes++;
    }
#if CONFIG_RK_FRAME_DEEP_SLEEP
    ESP_LOGI(TAG, "Frame Deep-sleep enabled; wake=%s, timeout=configured",
             s_timer_wake ? "timer" : "cold-or-key");
#else
    ESP_LOGI(TAG, "Frame Deep-sleep disabled by build profile");
#endif
}

void frame_power_manager_poll(bool runtime_transition_pending) {
    frame_power_snapshot_t snapshot = {0};
    frame_power_decision_t decision =
        build_snapshot(runtime_transition_pending, false, &snapshot);

    if (decision != s_last_decision) {
        ESP_LOGI(TAG, "Deep-sleep policy: %s", decision_name(decision));
        s_last_decision = decision;
    }

    if (decision != FRAME_POWER_READY) {
        if (s_state == FRAME_SLEEP_QUIESCING_BLE) {
            (void)cancel_sleep_attempt();
        }
        return;
    }
    if (snapshot.now_ms < s_retry_not_before_ms) {
        return;
    }

    if (s_state == FRAME_SLEEP_IDLE) {
        power_debug_rtc_init();
        s_power_debug_rtc.attempts++;
        s_power_debug_rtc.last_preflight_flags = 0;
        s_power_debug_rtc.last_preflight_error =
            PLATFORM_POWER_PREFLIGHT_ERROR_NONE;
        if (!configure_wake_sources()) {
            s_retry_not_before_ms = snapshot.now_ms + FRAME_RETRY_COOLDOWN_MS;
            return;
        }
        frame_ble_sleep_status_t ble =
            ble_hid_host_frame_prepare_for_sleep();
        if (ble == FRAME_BLE_SLEEP_FAILED) {
            s_power_debug_rtc.last_preflight_error =
                PLATFORM_POWER_PREFLIGHT_ERROR_BLE;
            ESP_LOGW(TAG, "BLE quiesce rejected; staying awake");
            (void)cancel_sleep_attempt();
            return;
        }
        s_state = FRAME_SLEEP_QUIESCING_BLE;
        if (ble != FRAME_BLE_SLEEP_READY) {
            return;
        }
        s_power_debug_rtc.last_preflight_flags |=
            PLATFORM_POWER_PREFLIGHT_BLE_OFF;
    } else {
        frame_ble_sleep_status_t ble =
            ble_hid_host_frame_prepare_for_sleep();
        if (ble == FRAME_BLE_SLEEP_PENDING) {
            return;
        }
        if (ble == FRAME_BLE_SLEEP_FAILED) {
            s_power_debug_rtc.last_preflight_error =
                PLATFORM_POWER_PREFLIGHT_ERROR_BLE;
            ESP_LOGW(TAG, "BLE did not quiesce; staying awake");
            (void)cancel_sleep_attempt();
            return;
        }
        s_power_debug_rtc.last_preflight_flags |=
            PLATFORM_POWER_PREFLIGHT_BLE_OFF;
    }

    // Re-read VBUS immediately before teardown so a recently attached cable
    // cannot be hidden by the one-second connected-idle cache.
    decision = build_snapshot(runtime_transition_pending, true, &snapshot);
    if (decision != FRAME_POWER_READY) {
        s_power_debug_rtc.last_preflight_error =
            PLATFORM_POWER_PREFLIGHT_ERROR_POLICY;
        (void)cancel_sleep_attempt();
        return;
    }

    ESP_LOGI(TAG, "Entering ESP32-S3 Deep-sleep; KEY or 30-minute timer wakes");
    s_power_debug_rtc.last_preflight_flags |=
        PLATFORM_POWER_PREFLIGHT_DISPLAY_SAFE;
    captive_portal_stop();
    platform_input_shutdown();
    s_power_debug_rtc.last_preflight_flags |=
        PLATFORM_POWER_PREFLIGHT_OUTPUTS_SAFE;
    wifi_mgr_stop();
    s_power_debug_rtc.last_preflight_flags |=
        PLATFORM_POWER_PREFLIGHT_WIFI_OFF;
    s_power_debug_rtc.preflight_completions++;
    s_power_debug_rtc.entries++;
    s_debug_force_not_before_ms = 0;
    esp_deep_sleep_start();
}

void frame_power_manager_debug_enrich(platform_power_diagnostics_t *out) {
    if (!out) {
        return;
    }
    power_debug_rtc_init();
#if CONFIG_RK_FRAME_DEEP_SLEEP
    out->capabilities = PLATFORM_POWER_CAP_SOC_DEEP_SLEEP |
        PLATFORM_POWER_CAP_RTC_EVIDENCE |
        PLATFORM_POWER_CAP_FORCED_TEST;
#endif
    out->deep_sleep_timer_active = s_debug_force_not_before_ms > 0;
    out->debug_sleep_override_armed = s_debug_force_not_before_ms > 0;
    out->debug_sleep_arms = s_debug_arm_count;
    out->power_off_attempts = s_power_debug_rtc.attempts;
    out->preflight_completions = s_power_debug_rtc.preflight_completions;
    out->power_off_entries = s_power_debug_rtc.entries;
    out->hardware_wakes = s_power_debug_rtc.hardware_wakes;
    out->last_preflight_flags = s_power_debug_rtc.last_preflight_flags;
    out->last_preflight_error = s_power_debug_rtc.last_preflight_error;
    out->reset_reason = s_power_debug_rtc.reset_reason;
    out->wakeup_cause = s_power_debug_rtc.wakeup_cause;
}

bool frame_power_manager_debug_arm(uint32_t delay_sec) {
#if CONFIG_RK_FRAME_DEEP_SLEEP
    if (delay_sec < 5 || delay_sec > 300) {
        return false;
    }
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
