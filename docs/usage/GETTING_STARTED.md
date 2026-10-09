# Getting Started from Scratch

HiPhi has two parts: a physical controller and [Unified Hi-Fi Control](https://github.com/open-horizon-labs/unified-hifi-control) (UHC), the bridge that connects it to your music system. Run the bridge on your home network, flash the correct controller over USB, then connect its Wi-Fi and choose a room.

## Choose the Exact Hardware

Start with the [controller comparison](https://hiphi.audio/controllers.html) or the [firmware target list](../../README.md#the-family). Match the manufacturer model and revision before buying or flashing. Each controller needs its own firmware even when two boards use the same ESP32 chip.

The example below uses **HiPhi Dial on the Waveshare ESP32-S3-Knob-Touch-LCD-1.8**. Other controllers use the same bridge and channel chooser; their [target notes](../targets/README.md) describe the hardware and controls.

You need a supported board, a data-capable USB cable, a computer with a current **Chrome, Edge, or Firefox** browser, and a nearby 2.4 GHz Wi-Fi network. iPhone, iPad, and Android can browse releases and configure Wi-Fi, but cannot perform USB flashing.

## Part 1: Run the Control Service

Install UHC on an always-on computer or NAS on the same network as your music system and controller. Use the [UHC setup documentation](https://github.com/open-horizon-labs/unified-hifi-control#quick-start-docker) for the current Docker, native, NAS, and LMS options.

For a Linux Docker host, create a folder and a Compose file:

```bash
mkdir -p ~/hiphi
cd ~/hiphi
```

Save this as `docker-compose.yml`:

```yaml
services:
  unified-hifi-control:
    image: muness/unified-hifi-control:latest
    restart: unless-stopped
    network_mode: host
    volumes:
      - unified-hifi-control-data:/data
    environment:
      - CONFIG_DIR=/data

volumes:
  unified-hifi-control-data:
```

Then start the bridge:

```bash
docker compose up -d
```

Open `http://<bridge-host>:8088`, using your host's LAN address, and confirm that your playback rooms appear. This Compose example needs host networking for music-system discovery. On macOS or Windows, follow the native installation or Docker networking instructions in the UHC documentation.

**Roon users:** open Roon → Settings → Extensions and enable **Unified Hi-Fi Control**. Users of LMS or OpenHome do not need Roon authorization.

If the bridge will not start, run `docker compose logs` in the folder containing the Compose file. Fix bridge discovery before setting up the controller.

## Part 2: Flash the Firmware

Open the [firmware center](https://firmware.hiphi.audio/) and choose Stable, Beta, or Alpha. The channel page and release notes identify the exact hardware and its validation status. Beta and Alpha are opt-in USB installations; they are not delivered automatically to Stable devices.

For the Waveshare Dial:

1. Turn on the device with the power slider toward the USB-C port.
2. Connect a data-capable USB-C cable to your computer.
3. Open the chosen channel’s **Dial** installer. **Click "Flash Dial main controller"**.
4. Select the serial port. Continue only when the installer identifies **ESP32-S3**. If it identifies **ESP32**, cancel, unplug the cable at the Dial end, rotate the plug 180°, and reconnect.
5. On a first installation, allow erase. During an update on the same controller, decline erase to keep Wi-Fi and controller settings.
6. Wait for the installer to finish. The time depends on the image, USB connection, and computer.

The Dial's second processor uses a separate one-time **auxiliary parking image**. If the channel includes it, follow its chip check: flip the USB-C plug at the Dial end and continue only when the installer identifies **ESP32**, not ESP32-S3. Then return the plug to the main-controller orientation.

**Command-line installation:** follow [Firmware Flashing](FIRMWARE_FLASHING.md) for the complete bootloader, partition table, OTA-data, and application commands. A fresh board needs all of these parts; writing only `hiphi_dial.bin` at `0x10000` does not install the full firmware. A downloaded merged factory image overwrites saved settings even without a separate erase command.

## Part 3: Connect Everything

On a fresh installation, the Dial creates the **hiphi-dial-setup** Wi-Fi network. Other targets use their own setup-network names.

1. Join the controller's setup Wi-Fi network with a phone or computer. Stay connected even if it reports no internet.
2. If the setup page does not appear, open **http://192.168.4.1** in a browser.
3. Select your nearby **2.4 GHz** home network and enter its password.
4. Reconnect your phone or computer to your home network after saving.
5. The controller discovers UHC through mDNS. Select the bridge and playback room when prompted.

An update that preserves settings should reconnect to its existing network instead of opening setup mode. See [Wi-Fi Provisioning](WIFI_PROVISIONING.md) if setup or reconnection fails.

Check what is playing, transport, volume, room selection, and reconnection after a reboot. Artwork depends on the controller's display and the music source. A successful flash alone does not confirm that all hardware functions work.

## Troubleshooting

| Problem | Next step |
|---------|-----------|
| No USB serial port appears | Try a data-capable cable, another USB port, and the target's download-mode procedure. On Dial, flip the plug at the controller end. |
| Wrong chip detected | Cancel. Confirm the board, selected installer, and Dial USB-C orientation. |
| Browser cannot flash | Open the channel page in a current desktop version of Chrome, Edge, or Firefox. Use the HTTPS firmware site. |
| Setup page does not appear | Stay on the controller's setup network and open `http://192.168.4.1`; temporarily disable mobile data if the phone routes around it. |
| Controller cannot find UHC | Confirm the bridge is running, authorized where required, and on the same network. Use the bridge's LAN address in controller settings if mDNS is blocked. |
| Wi-Fi fails | Check the password and 2.4 GHz network. Use the target's Forget Wi-Fi action to return to provisioning. |

## Updating

**Controller:** use the same exact target in the firmware center and decline erase for a settings-preserving browser update. HiPhi Dial also checks for Stable updates through the bridge and lets you start an available update in Settings. Beta and Alpha skip automatic checks; other targets require USB unless their own guide documents OTA. See [OTA Updates](OTA_UPDATES.md).

**Bridge:** in the Compose folder, run:

```bash
docker compose pull
docker compose up -d
```

For help, [open a firmware issue](https://github.com/muness/roon-knob/issues) with the controller model, firmware version, and what you observed, or visit the [Roon Community discussion](https://community.roonlabs.com/t/50-diy-roon-desk-controller/311363).
