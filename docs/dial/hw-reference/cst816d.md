Self-Capacitive Touch Controller CST816D – App-Facing Notes

Purpose
- Summarize only what matters to integrate a CST816D-based touch panel with our ESP32/LVGL firmware.
- Focus on wiring, init, event flow, and pitfalls that affect reliability.

What It Is
- Single‑touch, self‑capacitive touch controller with gesture detection.
- I2C interface with an interrupt output and a reset input.
- Suitable for low‑power, periodic polling or interrupt‑driven reads.

Electrical & Pins (module‑level, typical)
- Supply: 3.3 V typical (ensure local 0.1 µF + 1 µF decoupling at the controller/module).
- I2C: SDA/SCL with pull‑ups (4.7 kΩ–10 kΩ) to 3.3 V.
- INT: Active‑low interrupt line. Configure ESP32 GPIO with pull‑up and falling‑edge IRQ.
- RST: Active‑low reset. Hold low on power‑up for a clean start, then release.

I2C Interface
- 7‑bit I2C address: 0x15 (common default on CST816* modules; verify on your board).
- Bus speed: Standard/Fast mode (100–400 kHz) works reliably.

Reset & Bring‑Up (robust sequence)
- Drive RST low ≥10 ms after power is stable.
- Release RST high and delay ≥50 ms before the first I2C access.
- Clear any latched INT by performing an initial status read.

Runtime Model
- Interrupt‑driven preferred: on INT low, read current gesture/finger/coordinates over I2C, then return to idle.
- Polling fallback: if INT isn’t wired, poll every 10–20 ms and read only when a change is detected.
- Auto sleep: controller may enter low‑power; a touch wakes it and asserts INT.

Data You Read
- Gesture/status: indicates tap/long‑press/swipe directions (useful for button‑like UX if you don’t need coordinates).
- Coordinates: current X/Y for a single contact. Map to the panel pixel space used by LVGL.
- Finger count: present/absent gating for when to emit LVGL pressed/released.

Coordinate Mapping (360×360 round LCD)
- Raw axes may need one or more transforms:
  - Swap X/Y
  - Invert X and/or Y
  - Scale (raw range → 0..359)
  - Apply a circular mask if needed (discard points outside radius for round displays).

LVGL Integration Tips
- Create a single pointer device; on INT, read and post:
  - If finger present: send LVGL indev data with (x, y), state=LV_INDEV_STATE_PRESSED.
  - Else: state=LV_INDEV_STATE_RELEASED.
- Debounce: ignore noisy flutters by requiring 1–2 consistent reads before state change.
- Gesture optional: map swipe left/right to zone navigation, tap to play/pause, long‑press to open a menu.

Error Recovery & Robustness
- If I2C read fails or the controller becomes unresponsive, pulse RST low (≥10 ms), delay (≥50 ms), retry init.
- After brownouts or ESP32 deep‑sleep, always run the reset sequence.
- Avoid starving I2C: perform touch reads quickly inside ISR/task; do not block.

Wiring Checklist
- SDA/SCL pull‑ups present and routed cleanly.
- INT pulled up (internal or external) and connected to a GPIO capable of edge interrupt.
- RST connected to a controllable GPIO (recommended) or held high via pull‑up (less robust).
- Common ground between the touch module and ESP32; short, low‑impedance ground return.

Configuration Knobs in Our App
- I2C port, SDA/SCL pins, and frequency.
- INT and RST GPIO numbers (and their active polarity).
- Axis transforms and scaling for the specific panel.
- Optional gesture enable/disable and how gestures map to UI actions.

Known Gotchas
- Some mfg modules present the same default I2C address (0x15). If you add multiple panels on the bus, you’ll need address isolation.
- Hostname‑based discovery for the bridge doesn’t affect touch, but noisy ground or missing pull‑ups will—verify hardware first when touch is flaky.
- Self‑capacitive sensors can be sensitive to cabling and EMC. Keep the FPC short and avoid routing high‑speed signals under the sensor.

References
- Datasheet: CST816D, V1.3 (Waveshare mirror). Use it for register map details (gesture IDs, finger count, coordinate registers) and exact timing numbers.
