#include "platform/platform_power.h"

#include "sdkconfig.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include <string.h>

static const char *TAG = "power";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;
static bool s_cali_enabled;
static bool s_initialized;
static adc_channel_t s_battery_channel;
static adc_atten_t s_battery_atten;
static bool s_charge_gpio_configured;
static gpio_num_t s_charge_gpio = GPIO_NUM_NC;

#if CONFIG_RK_BATTERY_ADC_ATTEN_0DB
#define RK_ADC_ATTEN ADC_ATTEN_DB_0
#elif CONFIG_RK_BATTERY_ADC_ATTEN_2_5DB
#define RK_ADC_ATTEN ADC_ATTEN_DB_2_5
#elif CONFIG_RK_BATTERY_ADC_ATTEN_6DB
#define RK_ADC_ATTEN ADC_ATTEN_DB_6
#else
#define RK_ADC_ATTEN ADC_ATTEN_DB_11
#endif

static bool map_gpio_to_channel(int gpio, adc_channel_t *out_channel) {
    switch (gpio) {
        case 1: *out_channel = ADC_CHANNEL_0; return true;
        case 2: *out_channel = ADC_CHANNEL_1; return true;
        case 3: *out_channel = ADC_CHANNEL_2; return true;
        case 4: *out_channel = ADC_CHANNEL_3; return true;
        case 5: *out_channel = ADC_CHANNEL_4; return true;
        case 6: *out_channel = ADC_CHANNEL_5; return true;
        case 7: *out_channel = ADC_CHANNEL_6; return true;
        case 8: *out_channel = ADC_CHANNEL_7; return true;
        case 9: *out_channel = ADC_CHANNEL_8; return true;
        case 10: *out_channel = ADC_CHANNEL_9; return true;
        default:
            return false;
    }
}

static int fallback_raw_to_mv(int raw) {
    // Fallback scaling when calibration is unavailable.
    // Approximate full-scale voltage for the configured attenuation.
    int full_scale_mv;
    switch (RK_ADC_ATTEN) {
        case ADC_ATTEN_DB_0: full_scale_mv = 950; break;
        case ADC_ATTEN_DB_2_5: full_scale_mv = 1250; break;
        case ADC_ATTEN_DB_6: full_scale_mv = 1750; break;
        case ADC_ATTEN_DB_11: default: full_scale_mv = 2450; break;
    }
    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;
    return (raw * full_scale_mv) / 4095;
}

static void init_charge_gpio(void) {
#if CONFIG_RK_BATTERY_CHARGE_GPIO >= 0
    s_charge_gpio = (gpio_num_t)CONFIG_RK_BATTERY_CHARGE_GPIO;
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_RK_BATTERY_CHARGE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io_conf) == ESP_OK) {
        s_charge_gpio_configured = true;
    } else {
        ESP_LOGW(TAG, "Failed to configure charge GPIO %d", CONFIG_RK_BATTERY_CHARGE_GPIO);
        s_charge_gpio = GPIO_NUM_NC;
        s_charge_gpio_configured = false;
    }
#endif
}

static void init_calibration(void) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = s_battery_atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle) == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC calibration (curve fitting) enabled");
        return;
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id = ADC_UNIT_1,
        .atten = s_battery_atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&line_config, &s_cali_handle) == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC calibration (line fitting) enabled");
        return;
    }
#endif
    ESP_LOGW(TAG, "ADC calibration not available - using fallback scaling");
    s_cali_enabled = false;
}

void platform_power_init(void) {
    if (s_initialized) {
        return;
    }

    if (!map_gpio_to_channel(CONFIG_RK_BATTERY_ADC_GPIO, &s_battery_channel)) {
        ESP_LOGE(TAG, "Invalid battery ADC GPIO %d", CONFIG_RK_BATTERY_ADC_GPIO);
        return;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate ADC unit");
        return;
    }

    s_battery_atten = RK_ADC_ATTEN;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = s_battery_atten,
    };
    if (adc_oneshot_config_channel(s_adc_handle, s_battery_channel, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel");
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return;
    }

    init_calibration();
    init_charge_gpio();

    s_initialized = true;
    ESP_LOGI(TAG, "Battery monitoring initialized (GPIO %d)", CONFIG_RK_BATTERY_ADC_GPIO);
}

bool platform_power_get_status(struct platform_power_status *out_status) {
    if (!out_status) {
        return false;
    }
    memset(out_status, 0, sizeof(*out_status));

    if (!s_initialized || !s_adc_handle) {
        return false;
    }

    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, s_battery_channel, &raw) != ESP_OK) {
        ESP_LOGW(TAG, "ADC read failed");
        return false;
    }

    int sense_mv = 0;
    if (s_cali_enabled) {
        if (adc_cali_raw_to_voltage(s_cali_handle, raw, &sense_mv) != ESP_OK) {
            sense_mv = fallback_raw_to_mv(raw);
        }
    } else {
        sense_mv = fallback_raw_to_mv(raw);
    }

    int battery_mv = (sense_mv * CONFIG_RK_BATTERY_VDIV_NUM + (CONFIG_RK_BATTERY_VDIV_DEN / 2)) / CONFIG_RK_BATTERY_VDIV_DEN;
    if (battery_mv < 0) {
        battery_mv = 0;
    }

    out_status->voltage_mv = battery_mv;

    out_status->present = battery_mv >= CONFIG_RK_BATTERY_PRESENT_MIN_MV;

    int percent = -1;
    if (out_status->present && CONFIG_RK_BATTERY_VOLTAGE_MAX_MV > CONFIG_RK_BATTERY_VOLTAGE_MIN_MV) {
        if (battery_mv <= CONFIG_RK_BATTERY_VOLTAGE_MIN_MV) {
            percent = 0;
        } else if (battery_mv >= CONFIG_RK_BATTERY_VOLTAGE_MAX_MV) {
            percent = 100;
        } else {
            percent = (battery_mv - CONFIG_RK_BATTERY_VOLTAGE_MIN_MV) * 100 /
                      (CONFIG_RK_BATTERY_VOLTAGE_MAX_MV - CONFIG_RK_BATTERY_VOLTAGE_MIN_MV);
        }
    }
    out_status->percentage = percent;

    bool charging = false;
#if CONFIG_RK_BATTERY_CHARGE_GPIO >= 0
    if (s_charge_gpio_configured && out_status->present) {
        int level = gpio_get_level(s_charge_gpio);
#if CONFIG_RK_BATTERY_CHARGE_ACTIVE_LOW
        charging = (level == 0);
#else
        charging = (level != 0);
#endif
    }
#endif
    out_status->charging = charging;

    return true;
}
