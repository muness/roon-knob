# Firmware Flashing

This guide covers how to flash firmware to your Roon Knob hardware. There are two methods:
1. **Web Flasher** (recommended) - No tools to install, works in Chrome/Edge
2. **esptool.py** - Command-line tool for advanced users

## Web Flasher (Recommended)

The easiest way to flash firmware. Works directly in your browser using the Web Serial API.

### Requirements
- **Browser**: Chrome or Edge (version 89+). Safari and Firefox are NOT supported.
- **HTTPS**: Web Serial requires HTTPS. Use the hosted flasher at [roon-knob.muness.com](https://roon-knob.muness.com/flash.html).
- **USB cable**: Connect your ESP32 board to your computer

### Steps

1. Connect the ESP32-S3 board via USB-C
2. Turn on the device (power slider towards USB-C port)
3. Go to the **[Web Flasher](https://roon-knob.muness.com/flash.html)**
4. Click **"Flash ESP32-S3"**
5. Select the serial port when prompted
6. Wait ~30 seconds for flashing to complete

---

## esptool.py (Command Line)

For advanced users who prefer command-line tools or need more control.

### Installation

```bash
pip install esptool
```

### Download Firmware

Download the merged binary from [GitHub Releases](https://github.com/muness/roon-knob/releases/latest):
- `roon_knob_merged.bin` - ESP32-S3 firmware

### Flash ESP32-S3

```bash
# Put device in download mode first (BOOT + RST)
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x0 roon_knob_merged.bin
```

On macOS, the port is typically `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
On Windows, use `COM3` or similar.

### Erase Flash (Factory Reset)

To completely erase the flash before flashing (removes WiFi credentials, etc.):

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 erase_flash
```

### Flashing Individual Components

If you need to flash individual components instead of the merged binary:

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x10000 roon_knob.bin
```

---

## Troubleshooting

### "No serial port found"
- Make sure the device is connected and in download mode
- Install USB drivers if needed:
  - [CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (Silicon Labs)
  - [CH340 drivers](https://sparks.gogo.co.nz/ch340.html)

### "Failed to connect"
- Ensure you're holding BOOT while pressing RST/EN
- Try a different USB cable (some cables are charge-only)
- Try a different USB port

### "Wrong chip detected"
- Ensure the firmware matches your chip. Roon Knob uses ESP32-S3.

### "Access denied" or "Permission error"
- On Linux, add your user to the `dialout` group: `sudo usermod -a -G dialout $USER`
- On macOS, you may need to allow the serial port in System Preferences > Security

---

## After Flashing

After flashing a fresh device:
1. The device will create a WiFi access point named **"roon-knob-setup"**
2. Connect to this network with your phone or computer
3. A captive portal will open for WiFi configuration
4. Enter your WiFi credentials
5. The device will restart and connect to your network

See [WiFi Provisioning](WIFI_PROVISIONING.md) for more details.

---

## Technical Reference: esptool-js

The web flasher uses [esptool-js](https://github.com/espressif/esptool-js), Espressif's JavaScript implementation of esptool. It's wrapped by [ESP Web Tools](https://esphome.github.io/esp-web-tools/) which provides the user interface.

### How It Works

1. **Web Serial API**: Browser requests access to serial port
2. **Bootloader Protocol**: esptool-js implements the ESP32 serial bootloader protocol
3. **Flash Writing**: Binary is sent in chunks with checksums
4. **Verification**: MD5 checksum verifies successful flash

### Manifest Format

ESP Web Tools uses JSON manifests to describe firmware:

```json
{
  "name": "Roon Knob",
  "version": "1.0.0",
  "new_install_prompt_erase": true,
  "builds": [{
    "chipFamily": "ESP32-S3",
    "parts": [{
      "path": "https://example.com/firmware.bin",
      "offset": 0
    }]
  }]
}
```

Key fields:
- `chipFamily`: Must match connected chip (ESP32, ESP32-S3, ESP32-C3, etc.)
- `parts[].offset`: Flash address (use 0 for merged binaries)
- `new_install_prompt_erase`: Ask user about erasing flash on new installs

### Creating Merged Binaries

To create a merged binary from individual components:

```bash
esptool.py --chip esp32s3 merge_bin \
  -o merged.bin \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 8MB \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x10000 app.bin
```

This creates a single file that can be flashed at offset 0, containing all partitions at their correct addresses.
