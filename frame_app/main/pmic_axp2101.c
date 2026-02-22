// AXP2101 PMIC driver for PhotoPainter board (simplified, direct I2C register access)
// Adapted from PhotoPainter power_bsp.cpp — no XPowersLib dependency

#include "pmic_axp2101.h"

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

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
static bool s_initialized = false;

static esp_err_t pmic_read_reg(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static esp_err_t pmic_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

bool pmic_init(void) {
    // Configure I2C bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Add AXP2101 device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 100000,
    };

    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Verify device is responding
    uint8_t status;
    ret = pmic_read_reg(AXP2101_STATUS1, &status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 not responding");
        return false;
    }

    // Set VBUS current limit to 2A
    pmic_write_reg(0x15, 0x05);  // VBUS current limit = 2000mA

    // Set charge current to 200mA
    pmic_write_reg(0x62, 0x04);  // ICC = 200mA

    // Configure power outputs (matching original PhotoPainter firmware)
    // DC1 = 3.3V: (3300 - 1500) / 100 = 18 = 0x12
    pmic_write_reg(AXP2101_DC1_VOL, 0x12);

    // ALDO1-4 = 3.3V each: (3300 - 500) / 100 = 28 = 0x1C
    pmic_write_reg(AXP2101_ALDO1_VOL, 0x1C);
    pmic_write_reg(AXP2101_ALDO2_VOL, 0x1C);
    pmic_write_reg(AXP2101_ALDO3_VOL, 0x1C);
    pmic_write_reg(AXP2101_ALDO4_VOL, 0x1C);

    // Enable ALDO1-4 (bits 0-3 of register 0x90)
    uint8_t ldo_ctrl;
    pmic_read_reg(AXP2101_LDO_ONOFF0, &ldo_ctrl);
    ldo_ctrl |= 0x0F;  // Enable ALDO1-4
    pmic_write_reg(AXP2101_LDO_ONOFF0, ldo_ctrl);

    ESP_LOGI(TAG, "Power outputs configured: DC1=3.3V, ALDO1-4=3.3V (LDO ctrl=0x%02x)", ldo_ctrl);

    s_initialized = true;
    ESP_LOGI(TAG, "AXP2101 PMIC initialized (status=0x%02x)", status);
    return true;
}

bool pmic_is_charging(void) {
    if (!s_initialized) return false;
    uint8_t status;
    if (pmic_read_reg(AXP2101_STATUS1, &status) != ESP_OK) return false;
    // Bit 5 of STATUS1 indicates charging
    return (status & 0x20) != 0;
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
    // 14-bit ADC, 1.1mV per LSB
    int raw = ((h & 0x3F) << 8) | l;
    return raw;  // Already in mV (approximately)
}
