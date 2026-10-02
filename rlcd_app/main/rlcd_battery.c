#include "rlcd_battery.h"

#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <esp_timer.h>

#define RLCD_BATTERY_CHANNEL ADC_CHANNEL_3
#define RLCD_BATTERY_CACHE_US 15000000LL

static const char *TAG = "rlcd_battery";
static adc_oneshot_unit_handle_t s_adc;
static bool s_initialized;
static int s_cached_percent = -1;
static int64_t s_cached_us;

static bool ensure_adc(void) {
    if (s_initialized) return s_adc != NULL;
    s_initialized = true;
    const adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK) return false;
    const adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(s_adc, RLCD_BATTERY_CHANNEL, &channel) !=
        ESP_OK) {
        s_adc = NULL;
        return false;
    }
    return true;
}

int rlcd_battery_percent(void) {
    const int64_t now = esp_timer_get_time();
    if (s_cached_us && now - s_cached_us < RLCD_BATTERY_CACHE_US) {
        return s_cached_percent;
    }
    if (!ensure_adc()) return -1;
    int sum = 0;
    int valid = 0;
    for (int i = 0; i < 8; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, RLCD_BATTERY_CHANNEL, &raw) == ESP_OK) {
            sum += raw;
            ++valid;
        }
    }
    if (!valid) return -1;
    /* GPIO4 sees one third of VBAT. With 12 dB attenuation, the calibrated
     * nominal full-scale is 3.1 V. This is an intentionally conservative
     * estimate; the FNB-C2/runtime pass will calibrate the curve. */
    const int raw = sum / valid;
    const int battery_mv = raw * 3100 * 3 / 4095;
    s_cached_percent = (battery_mv - 3200) * 100 / 1000;
    if (s_cached_percent < 0) s_cached_percent = 0;
    if (s_cached_percent > 100) s_cached_percent = 100;
    s_cached_us = now;
    ESP_LOGD(TAG, "battery raw=%d estimate=%dmV %d%%", raw, battery_mv,
             s_cached_percent);
    return s_cached_percent;
}
