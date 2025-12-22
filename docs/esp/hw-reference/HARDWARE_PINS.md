# Hardware Pin Configuration

GPIO pin assignments for the Waveshare ESP32-S3 Touch AMOLED 1.8" (360×360).

## Display Pins (SH8601 AMOLED via QSPI)

| Function | GPIO | Notes |
|----------|------|-------|
| LCD_CS | 14 | SPI Chip Select |
| LCD_PCLK | 13 | SPI Clock |
| LCD_DATA0 | 15 | QSPI Data 0 |
| LCD_DATA1 | 16 | QSPI Data 1 |
| LCD_DATA2 | 17 | QSPI Data 2 |
| LCD_DATA3 | 18 | QSPI Data 3 |
| LCD_RST | 21 | Reset line |
| BK_LIGHT | 47 | Backlight (always on for AMOLED) |

**Display:** 360×360 round AMOLED with SH8601 controller using 4-wire QSPI interface.

## Input Pins

### Rotary Encoder

| Function | GPIO | Notes |
|----------|------|-------|
| ENCODER_A (ECA) | 8 | Quadrature channel A |
| ENCODER_B (ECB) | 7 | Quadrature channel B |

**Implementation:**
- Software quadrature decoding with polling (3ms interval)
- Software debouncing (2 consecutive stable reads)
- Internal pull-ups enabled
- Generates UI_INPUT_VOL_UP/VOL_DOWN events

**Note:** This device has NO physical buttons. The encoder shaft is NOT pressable. All button-like interactions use the touchscreen.

### Touch Controller (CST816D)

| Function | GPIO | I2C Details |
|----------|------|-------------|
| SDA | 11 | I2C_NUM_0, pull-up enabled |
| SCL | 12 | I2C_NUM_0, pull-up enabled |

| Parameter | Value |
|-----------|-------|
| I2C Address | 0x15 (7-bit) |
| I2C Speed | 300 kHz |
| Resolution | 12-bit (0-4095 raw → 0-359 display) |

**Implementation:**
- CST816D capacitive touch controller
- Integrated with LVGL as `LV_INDEV_TYPE_POINTER`
- Touch events handled automatically by LVGL input system

## Battery Monitoring (ADC)

| Function | GPIO | Notes |
|----------|------|-------|
| VBAT_ADC | 1 | ADC1_CH0, 12dB attenuation |

**Configuration:**
- Voltage divider: 2:1 (multiply ADC reading by 2)
- ADC range: 0-3.3V (reads 0-1.65V battery after divider)
- Full charge: ~4.2V, Empty: ~3.0V

## GPIO Availability Summary

| GPIO Range | Status | Usage |
|------------|--------|-------|
| 1 | **Used** | Battery ADC |
| 7-8 | **Used** | Rotary encoder (ECB, ECA) |
| 11-12 | **Used** | Touch I2C (SDA, SCL) |
| 13-18 | **Used** | Display QSPI |
| 21 | **Used** | Display reset |
| 47 | **Used** | Backlight |
| 0, 2-6, 9-10, 19-20, 22-46, 48 | **Available** | Expansion |

## Reserved/Strapping Pins

Be cautious with these ESP32-S3 pins:
- **GPIO0**: Boot mode selection (keep floating or pulled high)
- **GPIO45**: VDD_SPI voltage selection
- **GPIO46**: Boot mode selection

## Implementation Files

- `idf_app/main/platform_display_idf.c` - Display pin definitions and SH8601 init
- `idf_app/main/platform_input_idf.c` - Encoder GPIO and polling
- `idf_app/main/platform_battery_idf.c` - Battery ADC configuration
