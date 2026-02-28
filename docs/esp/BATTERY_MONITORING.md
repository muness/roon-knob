# Battery Monitoring

This document covers battery voltage monitoring and percentage calculation for the Roon Knob.

## Overview

The ESP32-S3-Knob includes a LiPo battery with voltage monitoring via ADC. The firmware reads the battery voltage and converts it to a percentage using a discharge curve lookup table.

## Hardware Configuration

| Parameter | Value | Notes |
|-----------|-------|-------|
| ADC Unit | ADC1 | Primary ADC unit |
| ADC Channel | Channel 0 | GPIO1 |
| Attenuation | 12 dB | 0-3.3V input range |
| Resolution | 12-bit | 0-4095 raw values |
| Voltage Divider | 2.0x | Battery connects through divider |

### Voltage Divider

The battery voltage (3.0V - 4.2V) exceeds the ADC's 3.3V range. A 2:1 voltage divider scales it down:

```
Battery (4.2V max)
    │
   ┌┴┐
   │R│ R1
   └┬┘
    ├──────► ADC (GPIO1) ── max 2.1V
   ┌┴┐
   │R│ R2
   └┬┘
    │
   GND
```

Measured ADC voltage × 2.0 = Battery voltage

## LiPo Discharge Curve

LiPo batteries have a non-linear discharge curve. The firmware uses a 13-point lookup table with linear interpolation:

| Voltage | Percentage | Notes |
|---------|------------|-------|
| 4.20V | 100% | Fully charged |
| 4.15V | 95% | |
| 4.10V | 90% | |
| 4.00V | 80% | |
| 3.90V | 70% | |
| 3.80V | 60% | |
| 3.75V | 50% | Nominal voltage region |
| 3.70V | 40% | |
| 3.65V | 30% | |
| 3.60V | 20% | Low battery warning |
| 3.50V | 10% | Critical |
| 3.30V | 5% | |
| 3.00V | 0% | Cutoff - stop using |

### Discharge Curve Visualization

```
100% ─┬─────────────────────────────────────────────── 4.2V
     │ ╲
 90% ─┼──╲
     │    ╲
 80% ─┼─────╲
     │       ╲
 70% ─┼────────╲
     │          ╲
 60% ─┼───────────╲
     │             ╲
 50% ─┼──────────────╲      ← Flat region (3.7-3.8V)
     │                ╲
 40% ─┼─────────────────╲
     │                   ╲
 30% ─┼────────────────────╲
     │                      ╲
 20% ─┼───────────────────────╲
     │                         ╲
 10% ─┼──────────────────────────╲
     │                            ╲
  0% ─┴─────────────────────────────╲─────────────── 3.0V
      3.0V        3.5V        4.0V        4.2V
```

LiPo batteries spend most of their discharge time in the 3.6-3.9V range, making percentage estimation challenging in that region.

## API Reference

### battery.h

```c
// Initialize ADC and calibration
// Returns: true on success, false on hardware error
bool battery_init(void);

// Get battery voltage in volts
// Returns: 3.0-4.2V range, or 0.0 on error
float battery_get_voltage(void);

// Get battery percentage (0-100)
// Uses discharge curve interpolation
int battery_get_percentage(void);

// Check if device is charging/on USB
// Heuristic: voltage > 4.15V indicates USB power
bool battery_is_charging(void);
```

## Implementation Details

### ADC Sampling

The firmware takes 16 samples and averages them to reduce noise:

```c
#define NUM_SAMPLES 16

int raw_sum = 0;
for (int i = 0; i < NUM_SAMPLES; i++) {
    int raw_value = 0;
    adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw_value);
    raw_sum += raw_value;
    vTaskDelay(pdMS_TO_TICKS(1));  // 1ms between samples
}
int raw_avg = raw_sum / NUM_SAMPLES;
```

Total sampling time: ~16ms per reading.

### ADC Calibration

ESP-IDF provides factory-calibrated ADC correction via curve fitting:

```c
// With calibration (more accurate)
adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &voltage_mv);

// Fallback (if calibration fails)
voltage_mv = (raw_avg * 3300) / 4095;
```

Calibration compensates for ADC non-linearity and chip-to-chip variation.

### Voltage to Percentage

Linear interpolation between curve points:

```c
static int voltage_to_percentage(float voltage) {
    // Find bracketing points
    for (size_t i = 0; i < CURVE_SIZE - 1; i++) {
        if (voltage >= curve[i + 1].voltage) {
            // Interpolate
            float ratio = (voltage - v2) / (v1 - v2);
            return p2 + (int)((p1 - p2) * ratio);
        }
    }
    return 0;
}
```

### Charging Detection

Simple heuristic based on voltage:

```c
bool battery_is_charging(void) {
    float voltage = battery_get_voltage();
    return (voltage > 4.15f);
}
```

When USB is connected, the battery charges and voltage rises above 4.15V. This isn't 100% reliable - a dedicated charge status GPIO would be better.

## Known Limitations

### Voltage Reading Accuracy

The current implementation shows ~4.9V when powered by USB, higher than expected. This may be due to:
- Incorrect voltage divider ratio in hardware
- Charge circuit adding voltage
- ADC calibration issues

For production use, calibrate the `BATTERY_VOLTAGE_DIVIDER` constant against a multimeter reading.

### No Charge Status GPIO

The hardware may have a dedicated charge status pin from the battery management IC, but it's not currently used. The voltage-based heuristic is a workaround.

### Temperature Effects

LiPo voltage varies with temperature. Cold batteries show lower voltage at the same charge level. The current curve assumes room temperature operation.

## Usage Example

```c
// Initialize during boot
if (!battery_init()) {
    ESP_LOGW(TAG, "Battery monitoring unavailable");
}

// Periodic check (e.g., every 30 seconds)
float voltage = battery_get_voltage();
int percentage = battery_get_percentage();
bool charging = battery_is_charging();

if (percentage < 10 && !charging) {
    ui_show_low_battery_warning();
}

ESP_LOGI(TAG, "Battery: %.2fV (%d%%) %s",
         voltage, percentage,
         charging ? "charging" : "discharging");
```

## Power Considerations

### WiFi Impact

WiFi transmission causes ~500mA current spikes that can drop battery voltage momentarily. This can cause:
- Temporary percentage drops
- False low-battery readings
- Brownout resets (mitigated by brownout level 4)

Consider smoothing/filtering battery readings if rapid fluctuations are an issue.

### Display Impact

The display backlight draws significant current. At full brightness:
- USB power: no issue
- Battery power: reduces runtime

The display sleep feature helps conserve battery when idle.

## Implementation Files

| File | Purpose |
|------|---------|
| `esp_dial/main/battery.c` | ADC reading, voltage calculation, curve lookup |
| `esp_dial/main/battery.h` | Public API |
