# HiPhi v{{VERSION}}

This alpha brings HiPhi firmware to nine devices. The biggest change is how the
Waveshare HiPhi Dial, our most popular controller, sleeps while idle. It also
adds nearby Wi-Fi lists everywhere, expands BLE media-remote support, and gives
every controller the same power-debug page.

> **This is test firmware.** Install it yourself from the
> [Beta Web Flasher](https://roon-knob.muness.com/beta/flash.html). Alpha builds
> are never sent through automatic OTA. Every image builds and passes the
> shared software checks, but the exact release images still need physical
> regression testing. We have not measured a battery-life improvement yet.

## The Waveshare Dial now sleeps between jobs

The Waveshare ESP32-S3-Knob-Touch-LCD-1.8 contains two programmable
processors. This alpha changes the power behavior of both:

- **Wi-Fi modem sleep:** the Dial stays connected, but its radio can sleep
  between access-point beacons and controller requests.
- **Automatic Light-sleep:** the main ESP32-S3 lowers its clock and sleeps
  between jobs when no task or radio lock needs it.
- **Main ESP32-S3 Deep-sleep:** on battery, the display progresses through Art
  mode, dim, panel off, and finally ESP32-S3 Deep-sleep. BLE, Wi-Fi, the LCD,
  and the backlight shut down first. Turning the encoder wakes the Dial with a
  fresh boot and reconnect.
- **No inherited timer wake:** before Deep-sleep, the shared power preflight
  disables automatic Light-sleep and clears its temporary timer wake source.
  A direct powered test exposed and removed the timer reboot that had prevented
  the main ESP32-S3 from remaining asleep.
- **The second ESP32 is parked:** install the separate auxiliary parking image
  once. It mutes the audio DAC, disables every wake source, and immediately
  enters Deep-sleep on every boot. The board wires this chip's enable pin high,
  so firmware cannot literally cut its power.
- **Less repeated work while awake:** one shared power snapshot now supplies
  battery level and external-power state to the whole status cycle. The Dial's
  16-sample ADC reading is cached for 15 seconds instead of being repeated by
  the UI, polling, and sleep-policy callers.
- **Less log noise:** the routine two-second now-playing request, response, and
  parse messages and periodic memory reports now require debug logging. Power
  policy, shutdown, sleep, and wake events remain visible at normal log levels.

The default battery timeline is 30 seconds to Art mode, another 30 seconds to
dim, another 60 seconds to turn the panel off, then 20 minutes to Deep-sleep.
The server can override those values. While USB power is detected, panel sleep
and Deep-sleep remain disabled by default.

The Dial still chooses between its battery and plugged-in policies using a
voltage heuristic. A nearly full battery can temporarily receive the plugged-in
policy and stay awake longer than expected.

To test the sleep path while the Dial is plugged in, open `/power-debug` from
its connected settings page and start the one-time 15-second test. After the
encoder wakes it, the page reports whether shutdown preparation completed,
whether Deep-sleep was requested, and what caused the wake. These counters are
firmware evidence, not an ammeter; use an external meter to measure actual
current and to confirm the auxiliary ESP32's contribution.

The Dial works after installing the main firmware. For the lowest idle draw,
also park the board's otherwise-unused auxiliary ESP32 once:

1. Install the [HiPhi Dial firmware](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_dial_merged.bin) on the main ESP32-S3.
2. Once, flip the USB-C cable to the other orientation and install the
   [auxiliary ESP32 parking image](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_knob_aux_park_merged.bin).

## What changed since v2.6.0-beta.1

### Nearby Wi-Fi scanning on every controller

All nine physical controllers now scan for nearby 2.4 GHz networks during
setup. The scan does not stop the setup access point. You can select a visible
SSID, retry an empty or failed scan, or type a hidden network manually. The
Waveshare Dial shows the list in both captive setup and connected settings.

### BLE media remotes expanded to HiPhi RLCD

BLE media-remote support is not new in this alpha: v2.6.0-beta.1 already
included it for HiPhi Dial and HiPhi Frame. This release adds the same host to
HiPhi RLCD and makes Dial and Frame sleep without forgetting the enabled
setting, remembered remote, or Bluetooth bond.

A paired Bluetooth Low Energy HOGP remote can control play/pause, previous,
next, volume up, and volume down for the selected zone. Frame and RLCD enable
the feature by default. Dial includes it but leaves it off until you enable
**BLE Media Remote** in settings. This is support for a separate physical
remote; it does not make the HiPhi controller a Bluetooth keyboard or speaker.

### One power-debug page across all hardware

Every physical controller exposes `/power-debug` and
`/power-debug?format=json` from its connected settings page. The shared report
shows the power source, active timeouts, display state, reset and wake cause,
and the sleep or power-off capabilities that the exact target implements.

Dial and Frame also provide the one-time 15-second powered Deep-sleep test and
RTC-retained evidence. Other targets report that the forced test is unsupported
until their own power-off and wake paths can retain trustworthy evidence. The
endpoint is passive: it does no background polling when nobody opens it.

### HiPhi Frame now enters processor Deep-sleep

On battery, a stopped and idle Frame now shuts down BLE and Wi-Fi, sleeps its
e-paper controller, and puts the ESP32-S3 into Deep-sleep. The KEY input and a
timer can wake it. The code path and retained wake evidence are implemented;
actual PMIC rail current still needs measurement on the release image.

## Choose your hardware and firmware

The firmware profile is tied to the exact model or SKU below. Do not substitute
a similarly named revision.

| HiPhi controller | Exact hardware | Buy from the manufacturer | Firmware |
| --- | --- | --- | --- |
| **HiPhi Dial** | Waveshare ESP32-S3-Knob-Touch-LCD-1.8; choose a battery-included variant for portable use | [Waveshare](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) | [Main](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_dial_merged.bin) + [auxiliary parking image](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_knob_aux_park_merged.bin) |
| **HiPhi Frame** | Waveshare ESP32-S3-PhotoPainter, 7.3-inch E6 color e-paper | [Waveshare](https://www.waveshare.com/product/esp32-s3-photopainter.htm) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_frame_merged.bin) |
| **HiPhi RLCD** | Waveshare ESP32-S3-RLCD-4.2 | [Waveshare](https://www.waveshare.com/esp32-s3-rlcd-4.2.htm) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_rlcd_merged.bin) |
| **AtomS3 JoyStick Deck** | M5Atom JoyStick with M5AtomS3, SKU K137 | [M5Stack](https://shop.m5stack.com/products/atom-joystick-with-m5atoms3) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_joy_merged.bin) |
| **M5Stack Tough Console** | M5Stack Tough, SKU K034 | [M5Stack](https://shop.m5stack.com/products/m5stack-tough-esp32-iot-development-board-kit) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_tough_merged.bin) |
| **M5 Dial Lab** | Original M5Stack Dial, SKU K130 | [M5Stack EOL page](https://shop.m5stack.com/products/m5stack-dial-esp32-s3-smart-rotary-knob-w-1-28-round-touch-screen) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_m5dial_merged.bin) |
| **M5StickS3 Twist Remote** | M5StickS3, SKU K150 | [M5Stack](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_sticks3_merged.bin) |
| **M5Stack StopWatch Wrist Remote** | M5Stack StopWatch, SKU C152 | [M5Stack](https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_stopwatch_merged.bin) |
| **Kizz Playback Companion** | M5StackChan robot, SKU K151; no remote-control bundle required | [M5Stack](https://shop.m5stack.com/products/stackchan-kawaii-co-created-open-source-ai-desktop-robot) | [Download](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/hiphi_stackchan_merged.bin) |

The original M5 Dial K130 is end-of-life. Its K130-V11 replacement is a
different revision and is not supported by this image. The auxiliary parking
image belongs to the Waveshare HiPhi Dial; it is not a tenth controller.

[Download SHA-256 checksums](https://github.com/muness/roon-knob/releases/download/v{{VERSION}}/SHA256SUMS.txt)

## Install this alpha

Use the [Beta Web Flasher](https://roon-knob.muness.com/beta/flash.html), choose
your exact hardware, and decline the erase option if you want to keep existing
Wi-Fi and controller settings. The merged `.bin` downloads above are factory
images; writing one at address `0x0` erases saved settings.

The stable web flasher and stable OTA feed do not offer this alpha. Return to
the Beta Web Flasher to install a later test build.

If you run the companion control service in Docker Compose, update it with:

```console
docker compose pull
docker compose up -d
```
