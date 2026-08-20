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
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>

static const char *TAG = "frame_power";

#define FRAME_WAKE_GPIO GPIO_NUM_4
#define FRAME_TIMER_WAKE_US (30ULL * 60ULL * 1000000ULL)
#define FRAME_TIMER_WAKE_GRACE_MS (60ULL * 1000ULL)
#define FRAME_RETRY_COOLDOWN_MS (60ULL * 1000ULL)
#define FRAME_SOURCE_CACHE_MS 1000ULL

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
        return false;
    }
    if (esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) != ESP_OK ||
        rtc_gpio_pullup_en(FRAME_WAKE_GPIO) != ESP_OK ||
        rtc_gpio_pulldown_dis(FRAME_WAKE_GPIO) != ESP_OK ||
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,
                            ESP_PD_OPTION_ON) != ESP_OK ||
        esp_sleep_enable_ext1_wakeup_io(1ULL << FRAME_WAKE_GPIO,
                                        ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK ||
        esp_sleep_enable_timer_wakeup(FRAME_TIMER_WAKE_US) != ESP_OK) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        ESP_LOGE(TAG, "Could not configure KEY/timer wake; staying awake");
        return false;
    }
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
        .provisioning = wifi_mgr_is_ap_mode() || captive_portal_is_running(),
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
        if (!configure_wake_sources()) {
            s_retry_not_before_ms = snapshot.now_ms + FRAME_RETRY_COOLDOWN_MS;
            return;
        }
        frame_ble_sleep_status_t ble =
            ble_hid_host_frame_prepare_for_sleep();
        if (ble == FRAME_BLE_SLEEP_FAILED) {
            ESP_LOGW(TAG, "BLE quiesce rejected; staying awake");
            (void)cancel_sleep_attempt();
            return;
        }
        s_state = FRAME_SLEEP_QUIESCING_BLE;
        if (ble != FRAME_BLE_SLEEP_READY) {
            return;
        }
    } else {
        frame_ble_sleep_status_t ble =
            ble_hid_host_frame_prepare_for_sleep();
        if (ble == FRAME_BLE_SLEEP_PENDING) {
            return;
        }
        if (ble == FRAME_BLE_SLEEP_FAILED) {
            ESP_LOGW(TAG, "BLE did not quiesce; staying awake");
            (void)cancel_sleep_attempt();
            return;
        }
    }

    // Re-read VBUS immediately before teardown so a recently attached cable
    // cannot be hidden by the one-second connected-idle cache.
    decision = build_snapshot(runtime_transition_pending, true, &snapshot);
    if (decision != FRAME_POWER_READY) {
        (void)cancel_sleep_attempt();
        return;
    }

    ESP_LOGI(TAG, "Entering ESP32-S3 Deep-sleep; KEY or 30-minute timer wakes");
    captive_portal_stop();
    platform_input_shutdown();
    wifi_mgr_stop();
    esp_deep_sleep_start();
}
