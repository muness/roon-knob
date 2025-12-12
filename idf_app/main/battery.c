#include "battery.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Include platform_time for platform_millis()
#include "platform/platform_time.h"

static const char *TAG = "battery";

// Hardware configuration
#define BATTERY_ADC_UNIT        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_0  // GPIO1
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_12  // 0-3.3V range (was DB_11, renamed in newer IDF)
#define BATTERY_VOLTAGE_DIVIDER 2.0f  // From working demo (but reads ~4.9V on USB - needs investigation)
#define NUM_SAMPLES             16  // Average this many readings

// LiPo voltage thresholds
#define BATTERY_MAX_VOLTAGE     4.2f
#define BATTERY_MIN_VOLTAGE     3.0f
#define BATTERY_NOMINAL_VOLTAGE 3.7f

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_initialized = false;

// LiPo discharge curve lookup table (voltage -> percentage)
static const struct {
    float voltage;
    int percentage;
} s_discharge_curve[] = {
    {4.20f, 100},
    {4.15f, 95},
    {4.10f, 90},
    {4.00f, 80},
    {3.90f, 70},
    {3.80f, 60},
    {3.75f, 50},
    {3.70f, 40},
    {3.65f, 30},
    {3.60f, 20},
    {3.50f, 10},
    {3.30f, 5},
    {3.00f, 0},
};

#define DISCHARGE_CURVE_SIZE (sizeof(s_discharge_curve) / sizeof(s_discharge_curve[0]))

/**
 * @brief Interpolate percentage from voltage using discharge curve
 */
static int voltage_to_percentage(float voltage) {
    // Clamp to valid range
    if (voltage >= BATTERY_MAX_VOLTAGE) {
        return 100;
    }
    if (voltage <= BATTERY_MIN_VOLTAGE) {
        return 0;
    }

    // Find bracketing points in curve
    for (size_t i = 0; i < DISCHARGE_CURVE_SIZE - 1; i++) {
        if (voltage >= s_discharge_curve[i + 1].voltage) {
            // Linear interpolation between points
            float v1 = s_discharge_curve[i].voltage;
            float v2 = s_discharge_curve[i + 1].voltage;
            int p1 = s_discharge_curve[i].percentage;
            int p2 = s_discharge_curve[i + 1].percentage;

            float ratio = (voltage - v2) / (v1 - v2);
            int percentage = p2 + (int)((p1 - p2) * ratio);
            return percentage;
        }
    }

    return 0;
}

bool battery_init(void) {
    if (s_initialized) {
        ESP_LOGI(TAG, "Battery already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing battery monitoring");
    ESP_LOGI(TAG, "  ADC Unit: %d", BATTERY_ADC_UNIT);
    ESP_LOGI(TAG, "  ADC Channel: %d (GPIO1)", BATTERY_ADC_CHANNEL);
    ESP_LOGI(TAG, "  ADC Attenuation: %d (DB_12 = 0-3.3V)", BATTERY_ADC_ATTEN);
    ESP_LOGI(TAG, "  Voltage Divider: %.1fx", BATTERY_VOLTAGE_DIVIDER);

    // Configure ADC unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    ESP_LOGI(TAG, "Creating ADC unit...");
    esp_err_t err = adc_oneshot_new_unit(&init_config, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC unit: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "ADC unit created successfully");

    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = BATTERY_ADC_ATTEN,
    };
    ESP_LOGI(TAG, "Configuring ADC channel %d...", BATTERY_ADC_CHANNEL);
    err = adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return false;
    }
    ESP_LOGI(TAG, "ADC channel configured");

    // Configure calibration (ESP32-S3 uses curve fitting scheme)
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_LOGI(TAG, "Configuring ADC calibration...");
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Calibration failed, using fallback: %s", esp_err_to_name(err));
        s_cali_handle = NULL;  // Will use manual calculation
    } else {
        ESP_LOGI(TAG, "ADC calibration configured");
    }

    s_initialized = true;

    // Log initial reading with details
    ESP_LOGI(TAG, "Taking initial battery reading...");
    float voltage = battery_get_voltage();
    int percentage = battery_get_percentage();
    bool charging = battery_is_charging();
    ESP_LOGI(TAG, "Battery initialized: %.2fV (%d%%) %s",
             voltage, percentage, charging ? "[CHARGING]" : "[ON BATTERY]");

    return true;
}

float battery_get_voltage(void) {
    static bool first_read = true;

    if (!s_initialized || !s_adc_handle) {
        if (first_read) {
            ESP_LOGW(TAG, "battery_get_voltage called but not initialized (handle=%p)", s_adc_handle);
            first_read = false;
        }
        return 0.0f;
    }

    int raw_sum = 0;
    int successful_reads = 0;

    // Take multiple samples and average
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int raw_value = 0;
        esp_err_t err = adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw_value);
        if (err == ESP_OK) {
            raw_sum += raw_value;
            successful_reads++;
        } else if (first_read) {
            ESP_LOGW(TAG, "ADC read %d failed: %s", i, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (successful_reads == 0) {
        ESP_LOGW(TAG, "Failed to read ADC - all %d samples failed", NUM_SAMPLES);
        first_read = false;
        return 0.0f;
    }

    int raw_avg = raw_sum / successful_reads;

    // Convert to voltage
    int voltage_mv = 0;
    if (s_cali_handle != NULL) {
        adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &voltage_mv);
    } else {
        // Fallback: manual calculation
        voltage_mv = (raw_avg * 3300) / 4095;
    }

    // Scale up through voltage divider
    float adc_voltage = voltage_mv / 1000.0f;
    float battery_voltage = adc_voltage * BATTERY_VOLTAGE_DIVIDER;

    if (first_read) {
        ESP_LOGI(TAG, "First voltage reading:");
        ESP_LOGI(TAG, "  Raw ADC avg: %d (from %d samples)", raw_avg, successful_reads);
        ESP_LOGI(TAG, "  Voltage (mV): %d", voltage_mv);
        ESP_LOGI(TAG, "  ADC voltage: %.3fV", adc_voltage);
        ESP_LOGI(TAG, "  Battery voltage: %.3fV", battery_voltage);
        ESP_LOGI(TAG, "  Using %s", s_cali_handle ? "calibration" : "manual calculation");
        first_read = false;
    }

    return battery_voltage;
}

int battery_get_percentage(void) {
    float voltage = battery_get_voltage();
    if (voltage < 0.1f) {
        return 0;  // Invalid reading
    }
    return voltage_to_percentage(voltage);
}

bool battery_is_charging(void) {
    // Heuristic: if voltage is above 4.15V, likely charging/on USB
    // This is a simple check - could be improved with a dedicated GPIO
    float voltage = battery_get_voltage();
    return (voltage > 4.15f);
}
