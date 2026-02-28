# Battery Management Hardware

Hardware reference for battery monitoring and charging on the ESP32-S3-Knob-Touch-LCD-1.8.

## Battery Specifications

| Parameter | Value | Notes |
|-----------|-------|-------|
| Type | Lithium Polymer (LiPo) | 3.7V nominal |
| Voltage Range | 3.0V - 4.2V | 3.0V critical low, 4.2V fully charged |
| Capacity | 800mAh | Model: 102035 |
| Connector | PH1.25 2-pin | MX1.25 lithium battery socket |
| Included | Yes | Assembled before shipment |

## Battery Voltage Monitoring

### ADC Configuration

Based on similar Waveshare ESP32-S3 boards (e.g., ESP32-S3-LCD-1.28), battery voltage monitoring typically uses:

| Parameter | Value | Notes |
|-----------|-------|-------|
| **Likely GPIO Pin** | GPIO1 | ADC1_CH0 - **NEEDS VERIFICATION** |
| Alternative | GPIO10 | ADC1_CH9 - less common |
| ADC Unit | ADC1 | Required - ADC2 incompatible with WiFi |
| ADC Resolution | 12-bit | 0-4095 range |
| Reference Voltage | 3.3V | ESP32-S3 max ADC input |

### Voltage Divider

Battery voltage (up to 4.2V) exceeds ESP32-S3 ADC maximum (3.3V), requiring a voltage divider:

**Actual Hardware Configuration (VERIFIED):**
```
Battery+ ----[10K]----+----[10K]---- GND
                       |
                     GPIO1 (ADC)
```

- **R1 (High)**: 10kΩ
- **R2 (Low)**: 10kΩ
- **Divider Ratio**: 2.0:1 (4.2V → 2.1V at ADC, 4.9V USB → 2.45V at ADC)
- **Measured on USB**: ~4.86V (reads charging circuit voltage, not battery directly)
- **Measured on Battery**: 3.2V-4.2V range
- **Voltage Calculation**: `V_battery = ADC_reading * (3.3V / 4095) * 2.0`

**Note:** When USB is connected, the ADC measures the charging circuit voltage (~4.9V), not the battery voltage. This is expected behavior.

**Formula:**
```c
// ADC reading to voltage
float adc_voltage = (adc_reading / 4095.0f) * 3.3f;
// Scale back up through 2:1 divider
float battery_voltage = adc_voltage * 2.0f;
```

**Verified by:**
- Demo firmware (ESP32-S3-Knob-Touch-LCD-1.8-Demo) uses 2.0x
- BlueKnob project (joshuacant) uses 2.0x
- ESPHome config (KrX3D/WaveShare-Knob-Esp32S3) uses multiply: 2.0

### ADC Considerations

1. **Use ADC1 Only**: ADC2 cannot be used when WiFi is active on ESP32-S3
2. **Available ADC1 Pins**: GPIO1-GPIO10 (channels 0-9)
3. **Attenuation**: Use `ADC_ATTEN_DB_11` for 0-3.3V range (default 0-2.5V)
4. **Calibration**: ESP32-S3 supports two-point calibration for accuracy
5. **Sampling**: Average multiple readings (16-32 samples) to reduce noise

## Charging Management

### Charge Controller

Board includes "battery charging management module" with these characteristics:

| Parameter | Expected Value | Notes |
|-----------|---------------|-------|
| Charge IC | Unknown | Likely ETA6096 or similar |
| Charge Current | ~1A default | Adjustable via resistor |
| Charge Cutoff | 4.2V ± 1% | Standard LiPo charging |
| Over-discharge Protection | ~3.0V | Prevents battery damage |

### Charging Status Detection

**To Be Determined:**
- GPIO pin for charge status (CHRG output from IC)
- GPIO pin for power source detection (USB vs Battery)
- Possible I2C communication with charge IC

**Common Patterns:**
- Dedicated GPIO for charging indicator (active low when charging)
- GPIO to detect USB power presence (Vbus detection)
- Some ICs support I2C for advanced telemetry

## Battery Percentage Estimation

### LiPo Discharge Curve

LiPo batteries have a non-linear discharge characteristic:

| Voltage | Approximate % | State |
|---------|--------------|-------|
| 4.20V | 100% | Fully charged |
| 4.15V | 95% | |
| 4.10V | 90% | |
| 4.00V | 80% | |
| 3.90V | 70% | |
| 3.80V | 60% | |
| 3.75V | 50% | |
| 3.70V | 40% | Nominal voltage |
| 3.65V | 30% | |
| 3.60V | 20% | |
| 3.50V | 10% | Low battery warning |
| 3.30V | 5% | Critical - shut down |
| 3.00V | 0% | Damage threshold |

### Implementation Notes

1. **Lookup Table**: Use interpolation between voltage points
2. **Smoothing**: Apply exponential moving average to reduce fluctuations
3. **Load Compensation**: Voltage drops under load, rises when idle
4. **Temperature**: Consider temperature effects on voltage (if sensor available)
5. **Calibration**: Fine-tune curve based on actual battery behavior

## Power Management

### Display Backlight Control

| Parameter | Value | GPIO |
|-----------|-------|------|
| Backlight Pin | GPIO47 | PWM capable |
| PWM Frequency | 5 kHz typical | Flicker-free |
| Duty Cycle Range | 0-100% | 0% = off |

**Battery Saving Strategy:**
- Dim to 30% after 10s idle on battery
- Dim to 10% after 30s idle on battery
- Sleep display after 60s idle on battery
- Full brightness when USB powered or user active

### CPU Power Management

ESP32-S3 power modes for battery operation:

| Mode | CPU Freq | Power Draw | Use Case |
|------|----------|------------|----------|
| High Performance | 240 MHz | ~50-80mA | Active UI, network |
| Normal | 160 MHz | ~35-50mA | Idle with display on |
| Light Sleep | Dynamic | ~3-5mA | Display off, WiFi on |
| Deep Sleep | Off | ~10-20μA | Long idle (not practical) |

**Strategy:**
- Reduce CPU frequency during idle periods
- Use light sleep with periodic wake for network checks
- Keep WiFi connected for responsiveness

## Critical Boot Issue: Brownout Detection

### Problem

The firmware currently fails to boot on battery power due to **overly aggressive brownout detection** settings:

| Setting | Current Value | Issue |
|---------|--------------|-------|
| Brownout Level | 7 (2.80V) | Too high for battery operation |
| WiFi TX Power | 20 dBm (max) | Peak 500mA current draw |

**What happens:**
1. WiFi initialization draws up to 500mA
2. Battery voltage sags briefly (3.7V LiPo → ~3.3V regulator → dips below 2.80V threshold)
3. Brownout detector triggers reset
4. Device enters boot loop or fails to start

**Demo firmware works because:** It likely uses brownout level 4-5 (2.43V-2.50V) instead.

### Solution

Lower the brownout detection threshold to accommodate battery voltage sag:

| Level | Voltage | Recommended For |
|-------|---------|----------------|
| 7 | 2.80V | USB powered only (current) |
| 6 | 2.70V | - |
| 5 | 2.60V | Marginal |
| **4** | **2.50V** | **Recommended for battery** |
| 3 | 2.43V | Battery with margin |
| 2 | 2.37V | Aggressive (risky) |
| 1 | 2.26V | Not recommended |

**Add to `sdkconfig.defaults`:**
```ini
# Brownout detection - lower threshold for battery operation
# Level 4 (2.50V) allows WiFi current spikes without false triggers
CONFIG_ESP_BROWNOUT_DET=y
CONFIG_ESP_BROWNOUT_DET_LVL_SEL_4=y
CONFIG_ESP_BROWNOUT_DET_LVL=4
CONFIG_BROWNOUT_DET_LVL_SEL_4=y
CONFIG_ESP32S3_BROWNOUT_DET_LVL_SEL_4=y
CONFIG_BROWNOUT_DET_LVL=4
CONFIG_ESP32S3_BROWNOUT_DET_LVL=4
```

**Optional: Reduce WiFi power on battery:**
```c
// In wifi_manager.c or main_idf.c
esp_wifi_set_max_tx_power(44);  // 11 dBm instead of 20 dBm (44 = 0.25dBm units)
```

### Hardware Considerations

- 800mAh battery can sustain 500mA peaks, but regulator efficiency matters
- Consider adding 470μF bulk capacitor near ESP32-S3 VDD (hardware mod)
- Waveshare may have adequate capacitance already

## Action Items

### Immediate Fix Required

1. **Lower brownout detection to level 4** (see sdkconfig changes above)
2. **Test boot on battery** after rebuild
3. **Verify WiFi connects** without brownout
4. **Consider dynamic power management:**
   - Detect power source (USB vs battery)
   - Reduce TX power and CPU freq when on battery
   - Restore full power when USB connected

### Immediate Verification Needed

1. **Confirm GPIO1 for battery voltage ADC**
   - Check schematic from Waveshare wiki
   - Or test empirically with multimeter and firmware

2. **Identify charging status GPIO**
   - Look for CHRG or STAT pin from charge IC
   - Test voltage when USB connected vs disconnected

3. **Identify power source detection**
   - Find GPIO that detects USB Vbus presence
   - Or use battery voltage spike as heuristic

4. **Determine charge IC model**
   - Read part markings on hardware
   - Check if I2C communication available

### Testing Plan

1. **ADC Calibration**
   - Read raw ADC values at known battery voltages
   - Verify voltage divider ratio
   - Tune formula for accuracy

2. **Charging Detection**
   - Monitor GPIO states during charge/discharge
   - Verify charging LED behavior (if present)
   - Test charge current measurement

3. **Battery Life Testing**
   - Measure current draw in different modes
   - Calculate runtime estimates
   - Verify low battery shutdown

## References

- [ESP32-S3 ADC Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/adc.html)
- [Waveshare ESP32-S3-Knob Wiki](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8)
- [ESP32-S3-LCD-1.28 Reference](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28) (similar board)
- [LiPo Battery Characteristics](https://batteryuniversity.com/article/bu-808-how-to-prolong-lithium-based-batteries)

## Related Files

- `esp_dial/main/platform_display_idf.c` - Backlight control (GPIO47)
- `docs/references/HARDWARE_PINS.md` - GPIO assignments
- Future: `esp_dial/main/battery.c` - Battery monitoring driver
