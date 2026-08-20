
---

## Firmware maturity

- **Every v2.7 firmware target — Alpha:** the exact release artifacts are
  software-validated but have not completed physical regression testing. For
  the full HiPhi Dial power-saving path, install both the main firmware and the
  one-time auxiliary ESP32 parking image.
- **HiPhi Frame and HiPhi RLCD:** packaged for exact-hardware testing.
  Frame now enters ESP32-S3 Deep-sleep on battery when stopped and idle, with
  KEY and timer wake; PMIC rail-current qualification remains open.
- **AtomS3 JoyStick, M5Stack Tough, M5 Dial, M5StickS3, StopWatch, and
  Kizz:** compile- and policy-validated;
  exact-artifact physical testing is still required.

## HiPhi Dial power-saving changes

The exact Waveshare ESP32-S3-Knob-Touch-LCD-1.8 contains **two programmable
processors**, and both are part of the board's power budget. Install both the
main HiPhi Dial firmware and the one-time auxiliary ESP32 parking image to
exercise the full power-saving path:

- **Wi-Fi can rest while staying connected.** The main firmware no longer
  forces the radio into its fully awake mode. It uses modem sleep between
  beacons and controller polls while remaining associated with the access
  point.
- **The main ESP32-S3 can nap between jobs.** Dynamic frequency scaling and
  automatic Light-sleep are enabled whenever application tasks and radio locks
  permit them.
- **The existing Deep-sleep path now shuts down cleanly.** Before sleep, BLE is
  quiesced without deleting its enabled preference or bonds, Wi-Fi and the LCD
  panel stop, and the backlight pin is latched off. The encoder wake circuit
  uses the board's external pull-ups instead of keeping the RTC peripheral
  domain powered. Turning the knob wakes through a fresh boot and reconnect.
- **The otherwise-unused second ESP32 is parked.** The separate auxiliary image
  mutes the audio DAC, disables every wake source and RTC memory domain, and
  enters Deep-sleep immediately on every boot. The board hard-wires this chip's
  enable pin high, so this is the strongest software-off state available rather
  than literal power removal.

When the Dial is using its default battery policy during normal connected
operation, the UI enters Art mode after 30 seconds, dims 30 seconds later, turns
the panel off after a further 60 seconds, and puts the main ESP32-S3 into
Deep-sleep 20 minutes after that. Charging defaults keep panel sleep and
Deep-sleep disabled. Server-provided settings can change these timeouts.

These are source- and build-verified mechanisms, not a battery-life result. No
current, percentage improvement, or runtime number is claimed until the exact
release images are measured on the exact board. Fixed regulators, the charger,
display logic, haptic controller, microphone, SD interface, and other
board-level loads remain in the measurement budget. The current firmware also
selects battery versus charging policy with a voltage heuristic; a nearly full
battery can temporarily receive the charging policy, and that boundary still
needs exact-board measurement rather than an unqualified threshold change.

## Every firmware image

The [Beta Web Flasher](https://roon-knob.muness.com/beta/flash.html) contains
every target. Merged images for command-line flashing are linked directly:

- [HiPhi Dial](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_dial_merged.bin)
- [Waveshare Dial auxiliary ESP32 parking image](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_knob_aux_park_merged.bin)
- [HiPhi Frame](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_frame_merged.bin)
- [HiPhi RLCD](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_rlcd_merged.bin)
- [AtomS3 JoyStick Deck](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_joy_merged.bin)
- [M5Stack Tough](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_tough_merged.bin)
- [M5 Dial Lab](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_m5dial_merged.bin)
- [M5StickS3 Twist Remote](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_sticks3_merged.bin)
- [M5Stack StopWatch](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_stopwatch_merged.bin)
- [Kizz Playback Companion](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_stackchan_merged.bin)
- [SHA-256 checksums](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/SHA256SUMS.txt)

## Updating

**Alpha prereleases:** Install explicitly from the
[Beta Web Flasher](https://roon-knob.muness.com/beta/flash.html) or download the
release assets. Prereleases are not published through automatic OTA and do not
replace the stable web flasher.

**Stable releases (existing users):** Update the control service and restart it;
the knob can then update over Wi-Fi.

**Docker Compose:**
```yaml
# Unified Hi-Fi Control - supports Roon, Lyrion (LMS), and OpenHome
services:
  unified-hifi-control:
    image: docker.io/muness/unified-hifi-control:latest
    restart: unless-stopped
    network_mode: host
    environment:
      TZ: America/New_York
    volumes:
      - unified-hifi-control-data:/home/node/app/data
volumes:
  unified-hifi-control-data:
```

> **Note:** Legacy image `muness/roon-extension-knob` still works and receives the same updates.

Then: `docker compose pull && docker compose up -d`

---

<details>
<summary><b>First-time flashing instructions</b></summary>

### Web Flasher (Recommended)

For a prerelease, use the [Beta Web Flasher](https://roon-knob.muness.com/beta/flash.html).
For a stable release, use the [stable web flasher](https://roon-knob.muness.com/flash.html).

### Command Line (esptool.py)

```bash
pip install esptool
esptool.py --chip esp32s3 -p /dev/cu.usbmodem* write_flash 0x0 hiphi_dial_merged.bin
```

`roon_knob_merged.bin` is a byte-identical compatibility alias for `hiphi_dial_merged.bin`.

**Troubleshooting:** If you get "No serial data received", retry a few times or try another cable.

</details>
