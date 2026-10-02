Touch UI Handling – Patterns and Hooks

Input Path
- Hardware: CST816 capacitive touch over I2C.
- LVGL: Registered as a `LV_INDEV_TYPE_POINTER` in `platform_display_idf.c`.
  - Callback: `lvgl_touch_read_cb()` fills `lv_indev_data_t` with `(x,y)` and `PRESSED/RELEASED`.

Current Touch Hooks (implemented)
- Zone label tap → open zone picker:
  - In `common/ui.c`, the zone label (`s_zone_label`) installs:
    - `lv_obj_add_event_cb(s_zone_label, zone_label_event_cb, LV_EVENT_CLICKED, NULL);`
    - Handler dispatches `UI_INPUT_MENU` (opens the zone picker).
- Zone picker item tap → select zone:
  - Zone list buttons install `LV_EVENT_CLICKED` and update selection, then dispatch `UI_INPUT_PLAY_PAUSE` to confirm.

Recommended Additions (examples)
- Play/Pause on center tap:
  - Add an event cb on `s_paused_label` (the play/pause icon) or the central dial container:
    - `lv_obj_add_event_cb(s_paused_label, [](lv_event_t *e){ ui_dispatch_input(UI_INPUT_PLAY_PAUSE); }, LV_EVENT_CLICKED, NULL);`
- Long‑press to open settings/debug:
  - Use LVGL’s `LV_EVENT_LONG_PRESSED` on `s_zone_label` (or a small gear icon) to toggle a hidden menu:
    - `lv_obj_add_event_cb(s_zone_label, on_long_press, LV_EVENT_LONG_PRESSED, NULL);`
- Volume scrubbing by touch (optional):
  - Add an event cb on `s_volume_bar`; on `PRESSED`/`PRESSING`, map `x` → 0..100 and send absolute volume (`UI_INPUT_VOL_ABS` if added) or relative steps while sliding.

Screen Area Guidance (360×360)
- Top band (~25–60 px) hosts the zone label; tap opens the zone picker.
- Center area hosts track title/subtitle; you can make this area clickable for play/pause.
- Bottom area shows progress + play/pause icon; tapping the icon is a natural play/pause gesture.
- Avoid placing touch targets too close to the circular edge; keep ~12–16 px margin for comfort.

LVGL Event Cheatsheet
- `LV_EVENT_PRESSED` – finger down
- `LV_EVENT_PRESSING` – continuous while held (drag)
- `LV_EVENT_LONG_PRESSED` – single fire after long press timeout
- `LV_EVENT_LONG_PRESSED_REPEAT` – repeats during long hold
- `LV_EVENT_RELEASED` – finger up (regardless of click)
- `LV_EVENT_CLICKED` – short press + release inside the object

Example: center‑tap play/pause
```
// After creating s_paused_label in build_layout()
static void play_icon_clicked(lv_event_t *e) {
  (void)e; ui_dispatch_input(UI_INPUT_PLAY_PAUSE);
}
lv_obj_add_event_cb(s_paused_label, play_icon_clicked, LV_EVENT_CLICKED, NULL);
```

Example: long‑press zone label for hidden menu
```
static void zone_label_long(lv_event_t *e) {
  (void)e; /* open debug/settings overlay here */
}
lv_obj_add_event_cb(s_zone_label, zone_label_long, LV_EVENT_LONG_PRESSED, NULL);
```

Notes
- Touch coordinates are already mapped to the display space by `cst816_get_touch()`.
- If you add new gesture mappings, keep encoder and touch actions consistent (both should drive the same `ui_dispatch_input()` codes).

