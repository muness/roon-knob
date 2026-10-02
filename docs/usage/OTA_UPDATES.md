# OTA Updates

HiPhi Dial can install a Stable application image over Wi-Fi through Unified Hi-Fi Control (UHC). The bridge downloads the latest Stable release, and the Dial checks for an available version. **You start installation in Settings; checking for updates does not install them automatically.**

This OTA route currently serves Dial firmware. Other controllers need their exact USB installer unless their target guide explicitly documents OTA support. An ESP32-S3 chip match alone is not enough to select another controller's image.

## Release Channels

| Channel | Installation | Update behavior |
|---------|--------------|-----------------|
| [Stable](https://firmware.hiphi.audio/stable/) | USB; Dial also supports OTA through UHC | The bridge uses GitHub’s `releases/latest`, which excludes prereleases. Dial checks automatically and offers the available update. |
| [Beta](https://firmware.hiphi.audio/beta/) | Opt-in USB | Prereleases do not enter the Stable feed. Dial skips automatic checks while running Beta. |
| [Alpha](https://firmware.hiphi.audio/alpha/) | Opt-in USB | Prereleases do not enter the Stable feed. Dial skips automatic checks while running Alpha. |

On Dial, **Check for Update** in Settings forces a check even from a prerelease. It queries the Stable feed and does not select another Alpha or Beta. The version comparison must find a newer version before installation becomes available. Channel labels in Settings keep a prerelease visible after installation.

## Update the Dial

1. Confirm the Dial is connected to Wi-Fi and UHC.
2. Open Settings and choose **Check for Update** if no available update is shown.
3. Review the offered version and start the update.
4. Keep the controller powered and connected while the image downloads and validates.
5. After the controller reboots, check its firmware version, room, transport, volume, and reconnection.

OTA writes an application slot and keeps the NVS settings partition. For USB updates, use the exact target's Flash button and decline erase. Downloaded merged factory images overwrite settings even without a separate erase command. See [Firmware Flashing](FIRMWARE_FLASHING.md).

## What the Firmware Checks

The Dial sends its controller identity with the version and download requests. Before accepting an image, it checks the ESP32-S3 chip, 16 MB flash geometry, and Dial application identity. The ESP-IDF OTA APIs validate the completed image before it becomes the boot target.

A download or validation failure leaves the current firmware selected. It is still necessary to test sustained boot and hardware behavior after a successful update; a transfer completing does not prove those functions.

Implementation: [ota_update.c](../../idf_app/main/ota_update.c), [ota_update.h](../../idf_app/main/ota_update.h), and [ui_network.c](../../idf_app/main/ui_network.c).

## Dial Partition Layout

This table describes the current Dial build only. Do not reuse it for another target or assume an older release has the same application-slot sizes.

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| nvs | `0x9000` | 16 KB | Wi-Fi and controller settings |
| otadata | `0xd000` | 8 KB | OTA boot selection |
| phy_init | `0xf000` | 4 KB | PHY calibration |
| factory | `0x10000` | 2.5 MB | Factory application |
| ota_0 | `0x290000` | 2.5 MB | OTA slot A |
| ota_1 | `0x510000` | 2.5 MB | OTA slot B |

Source: [idf_app/partitions.csv](../../idf_app/partitions.csv). OTA chooses the next update slot rather than writing NVS or a merged factory image.

## Bridge Contract and Compatibility

The bridge serves `GET /firmware/version` and `GET /firmware/download`. Its version response identifies the available application version, size, and filename; it is not a browser-flashing manifest.

The released bridge still selects **roon_knob.bin**, the byte-identical compatibility alias for **hiphi_dial.bin**. Keep both assets available during the naming transition. [UHC issue #277](https://github.com/open-horizon-labs/unified-hifi-control/issues/277) tracks selecting the primary HiPhi asset. These generic endpoints currently do not provide a family-wide, per-target OTA catalog.

The bridge downloads Stable firmware from GitHub Releases. It does not use the Beta or Alpha web installer as an OTA feed. See the [UHC firmware service](https://github.com/open-horizon-labs/unified-hifi-control/blob/main/src/firmware.rs) for the current server implementation.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No update available | Confirm UHC has a newer Stable application image. A prerelease or a channel page alone does not enter the OTA feed. |
| No bridge configured or connection failed | Restore the Dial's connection to the selected UHC bridge before retrying. |
| Download or validation failed | Leave the current firmware running, inspect the offered release and bridge logs, and retry only after resolving the cause. |
| Wrong target or unsupported image | Use the exact target’s USB installer. Never rename another controller’s binary to the Dial compatibility filename. |
| Need to test a prerelease | Choose Beta or Alpha explicitly at the firmware center and install over USB. |

## Release Maintenance

The firmware workflow builds with the repository’s pinned ESP-IDF version, publishes application and component assets plus compatibility aliases, and updates the matching `/stable/`, `/beta/`, or `/alpha/` installer. See [Development](../dev/DEVELOPMENT.md) and the release workflow for the current process. Release promotion requires the maintainer's approval and physical checks of the exact artifact on its target hardware.
