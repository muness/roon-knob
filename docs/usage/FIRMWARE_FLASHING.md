# Firmware Flashing

Choose the exact controller and revision at the [firmware center](https://firmware.hiphi.audio/). Every target has its own installer; a shared ESP32 chip family does not make firmware interchangeable. The command-line examples below are for **HiPhi Dial on the Waveshare ESP32-S3-Knob-Touch-LCD-1.8**. See the [target notes](../targets/README.md) for other boards.

There are two methods:
1. **Web Flasher** (recommended) - No tools to install; use desktop Chrome, Edge, or Firefox
2. **esptool.py** - Command-line tool for advanced users

## Web Flasher (Recommended)

The easiest way to flash firmware. Works directly in your browser using the Web Serial API.

### Requirements
- **Browser**: A current desktop version of Chrome, Edge, or Firefox. Safari is not supported; iPhone, iPad, and Android cannot flash over USB.
- **HTTPS**: Web Serial requires HTTPS. Choose a channel at [firmware.hiphi.audio](https://firmware.hiphi.audio/).
- **USB cable**: Use a data-capable cable; charge-only cables cannot flash firmware.

### Steps

1. Choose a release channel and the exact controller. Check its release notes for physical-hardware status.
2. Connect the board over USB. For Dial, turn on the power slider toward the USB-C port.
3. Click the target’s **Flash** button and select its serial port. For Dial: Click **"Flash Dial main controller"**.
4. Confirm the detected chip. Dial main firmware requires **ESP32-S3**; its auxiliary parking image requires **ESP32**. If the chip is wrong, cancel and flip the USB-C plug at the Dial end.
5. Allow erase for a fresh install. Decline erase during an update on the same controller to retain Wi-Fi and settings.
6. Wait for completion, then follow the setup instructions below.

The browser installer writes individual components outside NVS when you decline erase. A downloaded **merged factory image** writes across NVS and removes settings even without a separate erase. Use the Flash button or the component command below for a settings-preserving update.

---

## esptool.py (Command Line)

For advanced users who prefer command-line tools or need more control.

### Installation

```bash
pip install esptool
```

### Download Firmware

Download all four component files **from the same release** for a complete first installation or a settings-preserving update. [Latest Stable](https://github.com/muness/roon-knob/releases/latest) excludes prereleases; use [all releases](https://github.com/muness/roon-knob/releases) when deliberately choosing Beta or Alpha:
- `hiphi_dial_bootloader.bin`
- `hiphi_dial_partition-table.bin`
- `hiphi_dial_ota_data_initial.bin`
- `hiphi_dial.bin`

`roon_knob_merged.bin` is a byte-identical compatibility alias for `hiphi_dial_merged.bin`.

### Install or Update ESP32-S3 Using Components

```bash
# Put device in download mode first (BOOT + RST)
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash \
  0x0 hiphi_dial_bootloader.bin \
  0x8000 hiphi_dial_partition-table.bin \
  0xd000 hiphi_dial_ota_data_initial.bin \
  0x10000 hiphi_dial.bin
```

These ranges deliberately omit the NVS partition at `0x9000–0xcfff`, preserving
same-device Wi-Fi and controller settings. On macOS, the port is typically `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
On Windows, use `COM3` or similar.

### Erase Flash (Factory Reset)

To completely erase the flash before flashing (removes WiFi credentials, etc.):

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 erase_flash
```

### Destructive Clean Install

`hiphi_dial_merged.bin` (and its `roon_knob_merged.bin` compatibility alias)
is a destructive factory image: writing it at offset `0x0` overwrites NVS and
removes Wi-Fi and controller settings. Use it only when you explicitly want a
clean install.

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x0 hiphi_dial_merged.bin
```

### Flashing Individual Components

If you need to flash individual components instead of the merged binary:

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x10000 hiphi_dial.bin
```

---

## Troubleshooting

### "No serial port found"
- Make sure the device is connected and in download mode
- Install USB drivers if needed:
  - [CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (Silicon Labs)
  - [CH340 drivers](https://sparks.gogo.co.nz/ch340.html)

### "Failed to connect"
- **Flip the USB-C cable 180°** - One orientation connects to the ESP32-S3, the other to the auxiliary ESP32. If you see the wrong chip or no response, flip the cable.
- Ensure you're holding BOOT while pressing RST/EN
- Try a different USB cable (some cables are charge-only)
- Try a different USB port

### "Wrong chip detected"
- **Flip the USB-C cable 180°** - The USB-C port connects to different chips depending on orientation.
- Ensure the firmware matches your chip. HiPhi Dial uses ESP32-S3.

### "Access denied" or "Permission error"
- On Linux, add your user to the `dialout` group: `sudo usermod -a -G dialout $USER`
- On macOS, you may need to allow the serial port in System Preferences > Security

---

## After Flashing

After flashing a fresh device:
1. The device will create a WiFi access point named **"hiphi-dial-setup"**
2. Connect to this network with your phone or computer
3. A captive portal will open for WiFi configuration
4. Enter your WiFi credentials
5. The device will restart and connect to your network

If the setup page does not appear, stay connected to the setup network and open **http://192.168.4.1**. Use a 2.4 GHz Wi-Fi network. An update that preserved settings normally reconnects without opening a setup network. See [WiFi Provisioning](WIFI_PROVISIONING.md) for more details.

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
  "name": "HiPhi Dial",
  "version": "1.0.0",
  "new_install_prompt_erase": true,
  "builds": [{
    "chipFamily": "ESP32-S3",
    "parts": [{
      "path": "hiphi_dial_bootloader.bin",
      "offset": 0
    }, {
      "path": "hiphi_dial_partition-table.bin",
      "offset": 32768
    }, {
      "path": "hiphi_dial_ota_data_initial.bin",
      "offset": 53248
    }, {
      "path": "hiphi_dial.bin",
      "offset": 65536
    }]
  }]
}
```

Key fields:
- `chipFamily`: Must match connected chip (ESP32, ESP32-S3, ESP32-C3, etc.)
- `parts[].offset`: Flash address for each component. Keep all parts outside NVS to preserve settings; offset 0 merged factory images overwrite it.
- `new_install_prompt_erase`: Ask user about erasing flash on new installs

### Creating Merged Binaries

To create a merged binary from individual components:

```bash
esptool.py --chip esp32s3 merge_bin \
  -o merged.bin \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 16MB \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x10000 app.bin
```

This creates a single file that can be flashed at offset 0, containing all partitions at their correct addresses.
