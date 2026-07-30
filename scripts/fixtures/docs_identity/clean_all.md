# Corrected wording, every guarded subject in one file

Every sentence below discusses a guarded subject using the corrected wording. This file must
produce **zero** violations. If a rule ever starts firing here, that rule has become a blanket
ban on mentioning the topic rather than a guard against the wrong assertion.

- The target is the Waveshare ESP32-S3-Knob-Touch-LCD-1.8, and no other product.
- The panel is a 1.8 inch round IPS LCD at 360×360 over Quad SPI.
- Waveshare declares 262K colours, 600 cd/m² brightness, and a 1200:1 contrast ratio.
- Brightness is a PWM backlight on GPIO 47, driven through LEDC with 8-bit duty resolution.
- The initial duty is CONFIG_RK_BACKLIGHT_NORMAL, whose Kconfig default is 100 of 255, about
  39 per cent; the dim level is 25 of 255, about 10 per cent.
- The vendor-declared panel driver IC is ST77916; the software component the firmware binds
  through is esp_lcd_sh8601, which is a different kind of thing entirely.
- Vendor-declared part numbers are ESP32-S3R8 for the primary SoC and ESP32-U4WDH for the
  secondary; the module package is not claimed here.
- Touch is a CST816-family part read as raw registers over I2C at address 0x15. The datasheet
  Waveshare links is the CST816D one, and the fitted marking is unverified.
- Turning the knob changes volume. Tapping the zone name opens the zone picker. A long press on
  the zone name opens settings. No physical button is read by the firmware.
