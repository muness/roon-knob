# M5Stack Tough (SKU K034) — App-Facing Hardware Notes

Purpose
- Summarize only what matters to bring up `tough_app/` firmware: chip, display,
  touch, and the PMIC sequencing the display/touch actually depend on.
- This is a distinct document from `board.md` (Dial/ESP32-S3), per #211 — Tough
  is a different chip family with a different PMIC, display bus, and touch
  controller. Do not merge or infer facts across the two documents.

Chip
- Classic ESP32 (ESP32-D0WDQ6-V3), NOT ESP32-S3. 16 MB flash, 8 MB PSRAM in
  **Quad** SPI mode (not Dial/Frame's Octal mode — classic ESP32 does not
  support Octal PSRAM at all).
- Source: M5Stack K034 datasheet (posted to #193); board-marking confirmation
  against the owner's physical unit is still outstanding (see #193).

PMIC: AXP192 (I2C address 0x34)
- Tough shares its display/touch power-sequencing chassis with the first-gen
  M5Stack Core2 (also AXP192 + ILI9342C). This is confirmed directly from
  M5Stack's own open-source graphics driver (`m5stack/M5GFX`,
  `src/M5GFX.cpp`, Core2/Tough autodetect branch — Apache-2.0/FreeBSD
  licensed), not inferred from product-family naming.
- Critically, **the LCD reset line and the touch controller's reset line are
  not wired to ESP32 GPIOs directly** — both are driven through AXP192 GPIO
  pins over I2C:
  - AXP192 GPIO4 → LCD (ILI9342C) RST
  - AXP192 GPIO1 → Touch (CHSC6540) RST
  - AXP192 LDO2 → LCD panel power (set to 3300 mV)
  - AXP192 LDO3 → LCD backlight power (voltage-controlled, not a PWM/LEDC
    GPIO like Dial/Frame's backlight)
- The AXP192 register writes below are transcribed from M5GFX's verified
  boot sequence for `board_M5StackCore2`/`board_M5Tough` (same PMIC init path
  for both boards; the two boards diverge only in which touch IC responds at
  0x2E vs 0x38). All register writes are `new = (old & mask) | data`
  (read-modify-write), matching AXP192's standard "OR bits into place, mask
  out overwrites" pattern used throughout M5GFX:
  1. `0x95 <- (old & 0x72) | 0x84` — GPIO4 function select (enable as GPIO)
  2. `0x28 <- (old & 0xFF) | 0xF0` — LDO2 voltage = 3300 mV (high nibble)
  3. `0x12 <- (old & 0xFF) | 0x04` — LDO2 enable (bit 2)
  4. `0x92 <- (old & 0xF8) | 0x00` — GPIO1 function select (N-MOS open-drain)
  5. delay
  6. `0x96 <- (old & 0xFD) | 0x00` — GPIO4 LOW (assert LCD RST)
  7. `0x94 <- (old & 0xFD) | 0x00` — GPIO1 LOW (assert touch RST)
  8. delay (reset pulse width)
  9. `0x96 <- (old & 0xFF) | 0x02` — GPIO4 HIGH (release LCD RST)
  10. `0x94 <- (old & 0xFF) | 0x02` — GPIO1 HIGH (release touch RST)
  11. delay (panel/touch boot time)
  12. `0x12 <- (old & 0xFF) | 0x08` — LDO3 enable (bit 3, backlight power on)
  13. `0x28 <- (old & 0xF0) | <code>` — LDO3 voltage (low nibble); M5GFX maps
      an 8-bit brightness input to a 0x0-0xF code. This Alpha does not expose
      a brightness API — it writes a single fixed code for "on" and leaves
      dimming as future work.
- TODO(hardware-verify): the fixed backlight code and the exact reset/settle
  delays above have not been measured on the owner's physical unit; M5GFX
  uses ~1 ms delays during its fast autodetect path, this firmware uses more
  conservative delays since Alpha bring-up favors reliability over boot speed.

Display: ILI9342C, 320x240, SPI (4-wire, not QSPI)
- Shared SPI bus pins (from M5GFX's Core2/Tough branch):
  - MOSI: GPIO23
  - MISO: GPIO38 (used for panel ID probe; not required for LVGL's
    write-only flush path)
  - SCLK: GPIO18
  - CS: GPIO5
  - D/C: GPIO15
  - RST: **not a direct GPIO — driven via AXP192 GPIO4, see above**
- Panel IC is register-compatible with the ILI9341 command set (industry
  standard for the ILI934x family); this firmware uses the
  `espressif/esp_lcd_ili9341` managed component rather than a bespoke
  ILI9342C driver.
- TODO(hardware-verify): ILI9342C-specific gamma/MADCTL default overrides
  have not been validated against physical hardware; the ILI9341 defaults are
  used as a starting point.

Touch: CHSC6540, I2C address 0x2E
- Shares the same I2C bus as the AXP192 PMIC: SDA=GPIO21, SCL=GPIO22, 400 kHz.
- INT: GPIO39 (active-low; this firmware polls rather than using the
  interrupt for Alpha simplicity — TODO(hardware-verify): confirm INT
  polarity/behavior on real hardware before adding interrupt-driven reads).
- RST: via AXP192 GPIO1, see above.
- Register protocol (verified directly from M5Stack's open-source
  `Touch_CHSC6540.cpp`/`.hpp` in `m5stack/M5GFX`, FreeBSD-licensed LovyanGFX
  driver — this is the same open-source code M5Stack ships for this exact
  board, not a generic capacitive-touch guess):
  - Init: write raw 2-byte command `{0x5A, 0x5A}` (no register prefix) to
    switch the controller's IRQ reporting mode.
  - Touch read: write register address `0x02` (touch point count), then a
    repeated-start read. Byte 0 of the read is the point count (0-2 on this
    controller). If count > 0, continue reading `count * 6 - 2` more bytes
    (registers auto-increment from 0x03 onward).
  - Per-touch-point layout (6 bytes per point, first point immediately after
    the count byte):
    - `data[0]`: X high nibble (`& 0x0F`)
    - `data[1]`: X low byte
    - `data[2]`: Y high nibble (`& 0x0F`)
    - `data[3]`: Y low byte
    - `data[4]`, `data[5]`: unused by this firmware (finger weight/misc per
      other CHSC6540-family controllers; not consumed here)
  - Coordinate range: X 0-319, Y 0-239 (no rotation/offset applied at the
    driver level in this Alpha).

BLE / Bluetooth
- Chip supports Classic BT + BLE, but this Alpha has BLE **permanently
  disabled** — see `tough_app/main/tough_capabilities.h` and #193/#191. No
  BLE component, Kconfig, or code exists anywhere under `tough_app/`.

Partition / OTA
- Factory-only partition table (no `ota_0`/`ota_1`), following Frame's
  precedent — see `tough_app/partitions.csv`. No OTA update path in this
  Alpha; reflash over USB (CH9102 USB-UART bridge) to update firmware.

Sources
- M5Stack K034 datasheet (posted to #193).
- `m5stack/M5GFX` (`src/M5GFX.cpp`, `src/lgfx/v1/touch/Touch_CHSC6540.{hpp,cpp}`)
  — read directly from GitHub during this session to source the AXP192
  sequencing and CHSC6540 register protocol above; both are Apache-2.0/
  FreeBSD-licensed open source M5Stack ships for this exact board, used here
  only as a factual reference for register addresses/values, not copied
  verbatim into this firmware's (differently-structured) C code.
- `espressif/esp-bsp` (`components/lcd/esp_lcd_ili9341/include/esp_lcd_ili9341.h`)
  for the ILI9341 panel driver API surface.
