// AXP2101 PMIC driver for PhotoPainter board (simplified, direct I2C register access)
// Adapted from PhotoPainter power_bsp.cpp — no XPowersLib dependency

#include "pmic_axp2101.h"

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "pmic";

#define AXP2101_ADDR       0x34
#define I2C_SDA_PIN        47
#define I2C_SCL_PIN        48

// AXP2101 register addresses (subset we need)
#define AXP2101_STATUS1       0x00
#define AXP2101_STATUS2       0x01
#define AXP2101_VBAT_H        0x34
#define AXP2101_VBAT_L        0x35
#define AXP2101_BAT_PERCENT   0xA4

// Power output control registers
#define AXP2101_DC_ONOFF      0x80   // DCDC on/off control
#define AXP2101_LDO_ONOFF0    0x90   // ALDO1-4, BLDO1-2 on/off
#define AXP2101_LDO_ONOFF1    0x91   // DLDO1-2, CPUSLDO on/off
#define AXP2101_DC1_VOL       0x82   // DCDC1 voltage
#define AXP2101_ALDO1_VOL     0x92   // ALDO1 voltage
#define AXP2101_ALDO2_VOL     0x93   // ALDO2 voltage
#define AXP2101_ALDO3_VOL     0x94   // ALDO3 voltage
#define AXP2101_ALDO4_VOL     0x95   // ALDO4 voltage

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static atomic_bool s_initialized = false;
static StaticSemaphore_t s_bus_lock_storage;
static SemaphoreHandle_t s_bus_lock;

static esp_err_t pmic_read_reg(uint8_t reg, uint8_t *val) {
    xSemaphoreTake(s_bus_lock, portMAX_DELAY);
    esp_err_t result = i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
    xSemaphoreGive(s_bus_lock);
    return result;
}

static esp_err_t pmic_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    xSemaphoreTake(s_bus_lock, portMAX_DELAY);
    esp_err_t result = i2c_master_transmit(s_dev, buf, 2, 100);
    xSemaphoreGive(s_bus_lock);
    return result;
}

// Bus clear before handing the pins to the controller. Release (never drive)
// a high level; a device holding SCL/SDA low must fail closed, not be powered
// through another pin. Bounded clock-stretch wait also covers a stuck slave.
static bool release_scl(void) {
    gpio_set_level(I2C_SCL_PIN, 1);
    for (int i = 0; i < 100; ++i) {
        if (gpio_get_level(I2C_SCL_PIN)) return true;
        esp_rom_delay_us(10);
    }
    return false;
}

// Input must remain enabled: gpio_get_level() reads zero on output-only pins.
static const char *recover_bus(void) {
    gpio_set_level(I2C_SDA_PIN, 1);
    gpio_set_level(I2C_SCL_PIN, 1);
    // gpio_set_direction alone leaves GPIO47/48 in their reset IOMUX
    // function (SPICLK_P/N). gpio_config selects GPIO as well as enabling input.
    gpio_config_t pins = {
        .pin_bit_mask = (1ULL << I2C_SDA_PIN) | (1ULL << I2C_SCL_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&pins) != ESP_OK) return "I2C pin setup failed";
    bool ok = release_scl();
    for (int i = 0; ok && i < 9; ++i) {
        gpio_set_level(I2C_SCL_PIN, 0);
        esp_rom_delay_us(5);
        ok = release_scl();
        esp_rom_delay_us(5);
    }
    if (ok) {
        gpio_set_level(I2C_SCL_PIN, 0);
        gpio_set_level(I2C_SDA_PIN, 0);
        esp_rom_delay_us(5);
        ok = release_scl();
        esp_rom_delay_us(5);
        gpio_set_level(I2C_SDA_PIN, 1); // STOP
        esp_rom_delay_us(5);
        ok = ok && gpio_get_level(I2C_SDA_PIN);
    }
    const bool scl_high = gpio_get_level(I2C_SCL_PIN);
    const bool sda_high = gpio_get_level(I2C_SDA_PIN);
    gpio_reset_pin(I2C_SDA_PIN);
    gpio_reset_pin(I2C_SCL_PIN);
    if (ok && scl_high && sda_high) return NULL;
    if (!scl_high && !sda_high) return "I2C SCL and SDA held low";
    if (!scl_high) return "I2C SCL held low";
    if (!sda_high) return "I2C SDA held low";
    return "I2C bus clear failed";
}

static const char *configure_panel_power(void) {
    // Preserve the established board voltages; each required rail must read
    // back before the display may be treated as powered.
    const uint8_t settings[][2] = {
        {AXP2101_DC1_VOL, 0x12}, {AXP2101_ALDO1_VOL, 0x1C},
        {AXP2101_ALDO2_VOL, 0x1C}, {AXP2101_ALDO3_VOL, 0x1C},
        {AXP2101_ALDO4_VOL, 0x1C},
    };
    uint8_t status;
    const esp_err_t probe = pmic_read_reg(AXP2101_STATUS1, &status);
    if (probe == ESP_ERR_TIMEOUT) return "AXP2101 communication timed out";
    if (probe != ESP_OK) return "AXP2101 did not respond";
    for (unsigned i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        uint8_t value;
        if (pmic_write_reg(settings[i][0], settings[i][1]) != ESP_OK ||
            pmic_read_reg(settings[i][0], &value) != ESP_OK || value != settings[i][1]) {
            ESP_LOGE(TAG, "Panel rail voltage verification failed (reg 0x%02x)", settings[i][0]);
            return "Panel rail voltage verification failed";
        }
    }
    uint8_t enabled;
    if (pmic_read_reg(AXP2101_LDO_ONOFF0, &enabled) != ESP_OK ||
        pmic_write_reg(AXP2101_LDO_ONOFF0, enabled | 0x0F) != ESP_OK ||
        pmic_read_reg(AXP2101_LDO_ONOFF0, &enabled) != ESP_OK || (enabled & 0x0F) != 0x0F) {
        return "Panel rail enable verification failed";
    }
    return NULL;
}

static const char *init_bus(void) {
    if (!s_bus) {
        const char *error = recover_bus();
        if (error) return error;
        i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0, .sda_io_num = I2C_SDA_PIN,
            .scl_io_num = I2C_SCL_PIN, .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&cfg, &s_bus) != ESP_OK) return "I2C controller setup failed";
    }
    if (!s_dev) {
        i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AXP2101_ADDR, .scl_speed_hz = 100000};
        if (i2c_master_bus_add_device(s_bus, &cfg, &s_dev) != ESP_OK) return "AXP2101 setup failed";
    }
    return NULL;
}

const char *pmic_verify_panel_power(void) {
    const char *error = init_bus();
    if (!error) {
        error = configure_panel_power();
        if (error) {
            // IDF's reset API does not serialize with telemetry transactions.
            xSemaphoreTake(s_bus_lock, portMAX_DELAY);
            esp_err_t reset = i2c_master_bus_reset(s_bus);
            xSemaphoreGive(s_bus_lock);
            if (reset != ESP_OK) {
                error = "I2C controller could not clear the bus";
            } else {
                for (int retry = 0; error && retry < 2; ++retry) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                    error = configure_panel_power();
                }
            }
        }
    }
    if (error) {
        s_initialized = false;
        ESP_LOGE(TAG, "Panel power unavailable: %s", error);
    } else if (!s_initialized) {
        if (pmic_write_reg(0x15, 0x05) != ESP_OK) ESP_LOGW(TAG, "VBUS limit write failed");
        if (pmic_write_reg(0x62, 0x04) != ESP_OK) ESP_LOGW(TAG, "Charge current write failed");
        s_initialized = true;
        ESP_LOGI(TAG, "AXP2101 panel rails verified");
    }
    return error;
}

bool pmic_init(void) {
    // app_main initializes the lock before starting UI and telemetry tasks.
    s_bus_lock = xSemaphoreCreateMutexStatic(&s_bus_lock_storage);
    return pmic_verify_panel_power() == NULL;
}

pmic_power_source_t pmic_power_source(void) {
    if (!s_initialized) return PMIC_POWER_SOURCE_UNKNOWN;
    uint8_t status;
    if (pmic_read_reg(AXP2101_STATUS1, &status) != ESP_OK) {
        return PMIC_POWER_SOURCE_UNKNOWN;
    }
    // AXP2101 register 0x00 bit 5 is VBUS-good, not charger activity.
    return (status & 0x20) ? PMIC_POWER_SOURCE_EXTERNAL
                           : PMIC_POWER_SOURCE_BATTERY;
}

bool pmic_is_charging(void) {
    return pmic_power_source() == PMIC_POWER_SOURCE_EXTERNAL;
}

int pmic_get_battery_percent(void) {
    if (!s_initialized) return -1;
    uint8_t pct;
    if (pmic_read_reg(AXP2101_BAT_PERCENT, &pct) != ESP_OK) return -1;
    if (pct > 100) pct = 100;
    return pct;
}

int pmic_get_battery_voltage(void) {
    if (!s_initialized) return -1;
    uint8_t h, l;
    if (pmic_read_reg(AXP2101_VBAT_H, &h) != ESP_OK) return -1;
    if (pmic_read_reg(AXP2101_VBAT_L, &l) != ESP_OK) return -1;
    // 14-bit ADC, 1mV per LSB
    int raw = ((h & 0x3F) << 8) | l;
    return raw;  // mV
}

bool pmic_prepare_for_deep_sleep(void) {
    if (!s_initialized) return false;
    uint8_t ldo_ctrl = 0;
    if (pmic_read_reg(AXP2101_LDO_ONOFF0, &ldo_ctrl) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read e-paper rail state");
        return false;
    }
    ldo_ctrl &= (uint8_t)~0x0F;
    if (pmic_write_reg(AXP2101_LDO_ONOFF0, ldo_ctrl) != ESP_OK) {
        ESP_LOGE(TAG, "Could not disable ALDO1-4 e-paper rails");
        return false;
    }
    ESP_LOGI(TAG, "Deep-sleep rails prepared: ALDO1-4 off");
    return true;
}
