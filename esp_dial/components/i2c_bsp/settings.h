#ifndef SETTINGS_H
#define SETTINGS_H

#include "driver/gpio.h"
#include "driver/i2c_types.h"

// I2C Configuration (matches hardware)
#define ESP32_SCL_NUM (GPIO_NUM_12)
#define ESP32_SDA_NUM (GPIO_NUM_11)

// Touch Controller (CST816)
#define TOUCH_ADDR 0x15

// Haptic Motor Controller (DRV2605)
#define DRV2605_ADDR 0x5A

#endif // SETTINGS_H
