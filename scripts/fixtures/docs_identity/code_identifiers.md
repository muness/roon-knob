# Real code identifiers stay exempt

Every span below carries a code signal — an underscore, call syntax, statement punctuation, a
hex literal, a path, or a filename extension — so it is quoted source rather than an assertion.
This file must produce zero violations.

The panel is created by `esp_lcd_new_panel_sh8601()`, declared in `esp_lcd_sh8601.h`, and the
init array has type `sh8601_lcd_init_cmd_t` with entries like `{0x36, {0x00}, 1, 0}`.
Touch lives at `0x15` and is read by `tpGetCoordinates()` in
`idf_app/components/lcd_touch_bsp/lcd_touch_bsp.c`.
Brightness comes from `CONFIG_RK_BACKLIGHT_NORMAL`, configured with `.duty = CONFIG_RK_BACKLIGHT_NORMAL`.
The component default array is `vendor_specific_init_default[]`; the pixel format follows
`panel_dev_config.bits_per_pixel`. Provenance is in `board.md`, enforced by
`scripts/check_docs_identity.py`.
