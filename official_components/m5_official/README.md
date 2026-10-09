# Official M5Stack board libraries

This component is build glue only. It compiles the immutable M5Stack libraries
checked in as Git submodules under `components/m5_official/vendor`:

- M5Dial `1.0.3` (`533c9d0198e72621d55a4d35f726d3d53a7e1431`)
- Atom-JoyStick `V0.0.1` (`a0ff778ddec4034a2bb8210f3ad7a2ed077f4568`)
- StackChan-BSP `1.1.0` (`f7ed40e6f5d9a1d08440cb926f3a0865b81882f8`)

Only the library for the exact firmware target is compiled. M5Unified and
M5GFX remain the common official abstraction for display, touch, buttons, IMU,
power, battery, and vibration. Espressif's official Arduino component `3.3.11`
provides the runtime expected by these M5Stack board libraries on ESP-IDF 5.5.
The wrapper compiles both M5Unified and M5GFX with the same `ARDUINO`
definition because their public headers contain Arduino-conditional types.
Mixing those modes corrupts by-value objects at `M5.begin(config_t)` and display
registration; M5Unified explicitly warns against the first mismatch.

HiPhi code may choose interaction semantics, visual presentation, and motion
choreography through these public APIs. It must not own board pins, bus
registers, servo packets, calibration, or copied hardware drivers. CI enforces
that boundary with `scripts/check_m5_hardware_boundary.py`.
