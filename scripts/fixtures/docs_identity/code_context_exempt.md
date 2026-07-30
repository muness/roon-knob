# Code context is exempt

Guarded phrases below appear only inside a fenced block, so they are quotations of source, not
assertions about the board. This file must produce zero violations.

```c
// Swap bytes for big-endian QSPI display (SH8601 expects big-endian RGB565)
// Panel: 360x360 round AMOLED, 16.7 million colors, no backlight, ESP32-S3-WROOM-1
// Press the knob to open the zone picker
// Backlight starts at 50% duty
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x36, (uint8_t[]){0x00}, 1, 0},
};
```

A link whose *target* contains a guarded word is also not an assertion, because only the link
text is prose: [a different Waveshare product](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)
and the bare autolink <https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8>.

Backtick spans are covered separately: `code_identifiers.md` proves that spans carrying a code
signal stay exempt, and `backtick_claims.md` proves that a bare product claim in backticks does
not.
