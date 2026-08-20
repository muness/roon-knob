
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

## Highlights since v2.6.0-beta.1

### BLE media remotes: carried forward, expanded, and sleep-safe

BLE media-remote support is **not new in this alpha**: v2.6.0-beta.1 already
shipped the shared host for HiPhi Dial and HiPhi Frame. This alpha carries that
support forward, adds it to HiPhi RLCD, and makes the Dial/Frame lifecycle safe
for real processor sleep:

- Pair a separate Bluetooth Low Energy HOGP media remote from the controller's
  settings page. Play/pause, previous, next, volume up, and volume down control
  the selected playback zone.
- Frame and RLCD enable the capability by default. Dial includes it but leaves
  it disabled until the owner turns on **BLE Media Remote** in settings.
- Before Dial or Frame enters processor Deep-sleep, the BLE host now stops
  transiently without deleting its enabled preference, remembered peer, or
  bond. A failed teardown keeps the controller awake instead of pretending the
  radio is safely off.

This is a BLE **host** for a physical media remote. It does not turn the HiPhi
controller into a Bluetooth keyboard, speaker, or Classic Bluetooth audio
device. Remote compatibility still depends on the exact HOGP report format.

### Nearby Wi-Fi scanning during setup

The shared Wi-Fi manager now performs non-blocking scans for visible 2.4 GHz
networks while keeping the setup access point available. Setup surfaces can
show nearby SSIDs, report an empty or failed scan, retry, and still accept a
manually entered hidden network.

Scanning is exposed in this alpha on HiPhi Frame, HiPhi RLCD, AtomS3 JoyStick,
M5Stack Tough, M5 Dial, M5StickS3, StopWatch, and Kizz. The classic Waveshare
HiPhi Dial captive page still uses manual SSID entry; do not describe scanning
as universal until that final setup surface is migrated.

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

## Supported hardware and where to buy it

Firmware profiles are exact-hardware contracts, not product-family guesses.
Use the model or SKU shown below; links go to the manufacturers' official
product/store pages, and regional availability can change.

| HiPhi firmware | Exact supported hardware | Official product/store page |
| --- | --- | --- |
| **HiPhi Dial** | Waveshare ESP32-S3-Knob-Touch-LCD-1.8; choose a battery-included variant for portable use | [Waveshare](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) |
| **HiPhi Frame** | Waveshare ESP32-S3-PhotoPainter, 7.3-inch E6 full-color e-paper | [Waveshare](https://www.waveshare.com/product/esp32-s3-photopainter.htm) |
| **HiPhi RLCD** | Waveshare ESP32-S3-RLCD-4.2 | [Waveshare](https://www.waveshare.com/esp32-s3-rlcd-4.2.htm) |
| **AtomS3 JoyStick Deck** | M5Atom JoyStick with M5AtomS3, SKU K137 | [M5Stack](https://shop.m5stack.com/products/atom-joystick-with-m5atoms3) |
| **M5Stack Tough Console** | M5Stack Tough, SKU K034 | [M5Stack](https://shop.m5stack.com/products/m5stack-tough-esp32-iot-development-board-kit) |
| **M5 Dial Lab** | Original M5Stack Dial, SKU K130 | [M5Stack EOL product page](https://shop.m5stack.com/products/m5stack-dial-esp32-s3-smart-rotary-knob-w-1-28-round-touch-screen) |
| **M5StickS3 Twist Remote** | M5StickS3, SKU K150 | [M5Stack](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit) |
| **M5Stack StopWatch Wrist Remote** | M5Stack StopWatch, SKU C152 | [M5Stack](https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3) |
| **Kizz Playback Companion** | M5StackChan robot, SKU K151; the optional remote-control bundle is not required | [M5Stack](https://shop.m5stack.com/products/stackchan-kawaii-co-created-open-source-ai-desktop-robot) |

The original M5 Dial K130 is end-of-life. M5Stack's replacement K130-V11 is a
different revision and has not been qualified for this firmware; do not
substitute it based only on the shared product name. The Waveshare Dial
auxiliary parking image is a second firmware image for the same physical HiPhi
Dial, not a tenth supported device.

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
