
---

## Firmware maturity

- **HiPhi Dial — Beta:** physically exercised on current hardware, including
  display, Wi-Fi, artwork, settings persistence, and BLE media-remote control.
- **HiPhi Frame — Alpha:** packaged for early hardware testing. The current
  shared-stack build has not completed equivalent Frame regression coverage;
  expect rough edges and report target-specific failures.

## Updating

**Beta/Alpha prereleases:** Install explicitly from the
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
