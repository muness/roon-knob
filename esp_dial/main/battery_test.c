/**
 * @file battery_test.c
 * @brief Battery voltage monitoring test utility
 *
 * This test code helps identify the correct GPIO pin for battery voltage
 * monitoring on the ESP32-S3-Knob-Touch-LCD-1.8.
 *
 * Expected: GPIO1 (ADC1_CH0) with 200K/100K voltage divider (3:1 ratio)
 * Alternative: GPIO10 (ADC1_CH9)
 *
 * To use: Enable this in CMakeLists.txt and run test via serial monitor
 */

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "battery_test";

// Test configurations for different possible battery monitoring pins
typedef struct {
    adc_channel_t channel;
    int gpio_num;
    const char *name;
} adc_test_config_t;

static const adc_test_config_t test_configs[] = {
    { ADC_CHANNEL_0, 1, "GPIO1 (ADC1_CH0)" },
    { ADC_CHANNEL_9, 10, "GPIO10 (ADC1_CH9)" },
};

#define NUM_TEST_CONFIGS (sizeof(test_configs) / sizeof(test_configs[0]))
#define VOLTAGE_DIVIDER_RATIO 3.0f  // 200K/(200K+100K) divider
#define NUM_SAMPLES 32

/**
 * Read battery voltage from a specific ADC channel
 */
static esp_err_t read_battery_voltage_channel(adc_oneshot_unit_handle_t adc_handle,
                                              adc_cali_handle_t cali_handle,
                                              adc_channel_t channel,
                                              float *voltage_out) {
    int raw_sum = 0;

    // Take multiple samples and average
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int raw_value = 0;
        esp_err_t err = adc_oneshot_read(adc_handle, channel, &raw_value);
        if (err != ESP_OK) {
            return err;
        }
        raw_sum += raw_value;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    int raw_avg = raw_sum / NUM_SAMPLES;

    // Convert to voltage using calibration
    int voltage_mv = 0;
    if (cali_handle != NULL) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_avg, &voltage_mv));
    } else {
        // Fallback: manual calculation
        voltage_mv = (raw_avg * 3300) / 4095;
    }

    // Scale up through voltage divider
    float adc_voltage = voltage_mv / 1000.0f;
    float battery_voltage = adc_voltage * VOLTAGE_DIVIDER_RATIO;

    *voltage_out = battery_voltage;

    ESP_LOGI(TAG, "  Raw: %d, ADC: %.3fV, Battery: %.3fV",
             raw_avg, adc_voltage, battery_voltage);

    return ESP_OK;
}

/**
 * Test all candidate ADC pins to find battery voltage
 */
void battery_test_scan_pins(void) {
    ESP_LOGI(TAG, "=== Battery Voltage Pin Detection ===");
    ESP_LOGI(TAG, "Testing candidate ADC pins for battery voltage...");
    ESP_LOGI(TAG, "Expected battery range: 3.0V - 4.2V");
    ESP_LOGI(TAG, "Expected ADC range: 1.0V - 1.4V (with 3:1 divider)");
    ESP_LOGI(TAG, "");

    // Configure ADC1 unit
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    // Configure calibration
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t cali_err = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
    if (cali_err != ESP_OK) {
        ESP_LOGW(TAG, "Calibration failed, using fallback calculation");
    }

    // Test each candidate pin
    for (int i = 0; i < NUM_TEST_CONFIGS; i++) {
        const adc_test_config_t *config = &test_configs[i];

        ESP_LOGI(TAG, "Testing %s...", config->name);

        // Configure channel
        adc_oneshot_chan_cfg_t chan_config = {
            .bitwidth = ADC_BITWIDTH_12,
            .atten = ADC_ATTEN_DB_11,  // 0-3.3V range
        };

        esp_err_t err = adc_oneshot_config_channel(adc1_handle, config->channel, &chan_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "  Failed to configure channel: %s", esp_err_to_name(err));
            continue;
        }

        // Read voltage
        float voltage = 0.0f;
        err = read_battery_voltage_channel(adc1_handle, cali_handle, config->channel, &voltage);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "  Failed to read: %s", esp_err_to_name(err));
            continue;
        }

        // Analyze result
        if (voltage >= 3.0f && voltage <= 4.5f) {
            ESP_LOGI(TAG, "  ✓ LIKELY BATTERY PIN - voltage in expected range!");
        } else if (voltage < 0.5f) {
            ESP_LOGI(TAG, "  ✗ Too low - probably not connected");
        } else if (voltage > 4.5f) {
            ESP_LOGI(TAG, "  ✗ Too high - check divider ratio or USB voltage");
        } else {
            ESP_LOGI(TAG, "  ? Uncertain - may need different divider ratio");
        }
        ESP_LOGI(TAG, "");
    }

    // Cleanup
    if (cali_handle != NULL) {
        adc_cali_delete_scheme_line_fitting(cali_handle);
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));

    ESP_LOGI(TAG, "=== Test Complete ===");
    ESP_LOGI(TAG, "Instructions:");
    ESP_LOGI(TAG, "1. Test with USB connected (should read ~4.2V if charging, or battery voltage)");
    ESP_LOGI(TAG, "2. Test on battery only (should read 3.5V-4.0V depending on charge level)");
    ESP_LOGI(TAG, "3. Verify reading changes when switching between USB and battery");
    ESP_LOGI(TAG, "4. Update battery.h with correct GPIO pin and divider ratio");
}

/**
 * Continuous battery monitoring for calibration
 */
void battery_test_monitor(adc_channel_t channel, int gpio_num) {
    ESP_LOGI(TAG, "=== Battery Monitoring Test ===");
    ESP_LOGI(TAG, "Monitoring %s GPIO%d continuously...",
             channel == ADC_CHANNEL_0 ? "ADC1_CH0" : "ADC1_CH9", gpio_num);
    ESP_LOGI(TAG, "Press Ctrl+] to stop");
    ESP_LOGI(TAG, "");

    // Configure ADC
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel, &chan_config));

    // Configure calibration
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);

    // Monitor loop
    while (1) {
        float voltage = 0.0f;
        read_battery_voltage_channel(adc1_handle, cali_handle, channel, &voltage);

        // Estimate percentage (simple linear approximation for now)
        float percentage = ((voltage - 3.0f) / 1.2f) * 100.0f;
        if (percentage < 0.0f) percentage = 0.0f;
        if (percentage > 100.0f) percentage = 100.0f;

        ESP_LOGI(TAG, "Battery: %.2fV (%.0f%%)", voltage, percentage);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* Note: These functions are not called automatically.
 * To use them:
 * 1. Add #include "battery_test.h" to main_idf.c
 * 2. Call battery_test_scan_pins() during initialization
 * 3. Or call battery_test_monitor() for continuous monitoring
 * 4. Check serial output for results
 */
