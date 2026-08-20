#include "platform/platform_power.h"

#include "controller_config.h"
#include "platform/platform_display.h"
#include "platform/platform_time.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_err.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <sdkconfig.h>
#endif

#if defined(__GNUC__)
#  define PLATFORM_WEAK __attribute__((weak))
#else
#  define PLATFORM_WEAK
#endif

PLATFORM_WEAK void platform_power_diagnostics_enrich(
    platform_power_diagnostics_t *out) {
    (void)out;
}

PLATFORM_WEAK bool platform_power_debug_arm_sleep(uint32_t delay_sec) {
    (void)delay_sec;
    return false;
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
}
