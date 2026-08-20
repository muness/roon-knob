
---

## Firmware maturity

- **Every v2.7 firmware target — Alpha:** the exact release artifacts are
  software-validated but have not completed physical regression testing. For
  minimum HiPhi Dial idle draw, install both the main firmware and the one-time
  auxiliary ESP32 parking image.
- **HiPhi Frame and HiPhi RLCD:** packaged for exact-hardware testing.
  Frame now enters ESP32-S3 Deep-sleep on battery when stopped and idle, with
  KEY and timer wake; PMIC rail-current qualification remains open.
- **AtomS3 JoyStick, M5Stack Tough, M5 Dial, M5StickS3, StopWatch, and
  Kizz:** compile- and policy-validated;
  exact-artifact physical testing is still required.

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
