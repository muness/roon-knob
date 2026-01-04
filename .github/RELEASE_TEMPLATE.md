
---

## Updating

**OTA (existing users):** Update the control service, restart, knob updates automatically.

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

Use the [web flasher](https://roon-knob.muness.com/flash.html) in Chrome/Edge - no tools to install.

### Command Line (esptool.py)

| File | Chip | USB Port |
|------|------|----------|
| `roon_knob_merged.bin` | ESP32-S3 | `usbmodem` |
| `esp32_bt_merged.bin` | ESP32 (BT) | `usbserial` |

```bash
pip install esptool

# Flash ESP32-S3 (USB-C normal orientation)
esptool.py --chip esp32s3 -p /dev/cu.usbmodem* write_flash 0x0 roon_knob_merged.bin

# Flip USB-C 180°, then flash ESP32
esptool.py --chip esp32 -p /dev/cu.usbserial-* write_flash 0x0 esp32_bt_merged.bin
```

**Troubleshooting:** If you get "No serial data received", retry a few times or try another cable.

</details>
