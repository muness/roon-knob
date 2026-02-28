Rotary Encoder (EC11‑class) – App‑Facing Notes

Overview
- Mechanically‑detented quadrature rotary encoder used for volume and navigation.
- Two digital outputs: Channel A (ECA) and Channel B (ECB).
- Our board’s shaft is rotation‑only (no push switch wired).

Electrical
- Open‑collector style contacts; require pull‑ups. Firmware enables internal pull‑ups.
- Connect A/B to GPIOs with input capability (we use ECA→GPIO 8, ECB→GPIO 7).
- Debounce is required due to contact bounce; we do it in software.

Firmware Implementation (current)
- Software quadrature decoder with a 3 ms polling timer.
- Debounce: 2 ticks (state must be stable across two polls before counting).
- Direction: A leads B → increment; B leads A → decrement.
- Events: deltas are mapped to UI_INPUT_VOL_UP / UI_INPUT_VOL_DOWN and queued; UI consumes and adjusts volume.

Recommended Alternatives
- Hardware PCNT peripheral for higher resolution/less CPU:
  - Configure two channels in 4× mode with a 1–2 µs glitch filter.
  - Generate an interrupt on count threshold, read deltas, and post UI events.
- Keep software as a fallback for portability/simplicity.

Tuning Knobs
- Poll interval (default 3 ms): lower for snappier feel, higher to reduce CPU.
- Debounce ticks (default 2): increase if your encoder is noisy.
- Direction swap: flip mapping if rotation feels inverted in your mechanical layout.
- Rate limiting: UI can coalesce multiple deltas to avoid flooding volume changes to Roon.

Testing
- Rotate slowly to verify single‑step events.
- Rotate quickly to ensure no missed steps or oscillations.
- Confirm direction matches UI expectations; swap A/B if needed.

References
- Implementation: esp_dial/main/platform_input_idf.c (ENCODER_GPIO_A/B, debounce, polling).
- Pinout: docs/references/HARDWARE_PINS.md.

