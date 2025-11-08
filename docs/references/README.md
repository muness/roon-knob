# Reference Notes

This folder caches upstream examples/snippets we can mine for patterns without dragging in entire third-party repos.

## Pulled into `docs/references/`

- `lvgl_pc/` – Extracted from LVGL's `lv_port_pc_vscode` repo (`src/main.c`, `src/hal/hal.{c,h}` and the README) to guide the SDL2 simulator wiring.
- `esp_box_snippets/` – Pieces of Espressif's ESP-BOX demos (`examples/lv_demos/main/*`) for LVGL init and menu handling on ESP32-S3.
- `tft_snippets/` – GC9A01 init tables plus notes from Bodmer’s TFT_eSPI library for the 240×240 round display glue.
- `lvgl_weather/` – The `lv_esp_idf` weather demo entrypoint (`display_lvgl_demos_main.c`) that initializes LVGL on an ESP32, starts the display backend, and runs `lv_demo_music()`/`lv_demo_widgets()`, providing a reference for the kind of LVGL polling/layout we need.
