# OTA Updates

Every HiPhi controller can install an application image over Wi-Fi through Unified Hi-Fi Control (UHC), and every target can ask UHC for it from the bridge. **Status: implemented, unverified on hardware.** The shared client builds and passes host tests for every target. No target's bridge-served update, rollback, or settings preservation has been exercised on a physical unit, and nothing here should be read as hardware-proven.

Two ways to start an update, both always explicit:

- **UHC "Update" button.** The bridge's device page offers an Update button when a newer image exists for the controller's channel. Pressing it adds a pending install request to that controller's configuration. The controller picks it up the next time it fetches its configuration, re-checks the bridge, and installs only a strictly newer version. All targets support this path.
- **On the Dial itself.** Settings has **Check for Update** and a start button. This on-device control is Dial only.

Checking for updates never installs anything by itself.

## One-time reflash for the new partition table

Earlier firmware has no second application slot. Before the first bridge-served update, each controller needs a **one-time browser reflash** that writes the new partition table (factory + `ota_0` + `ota_1`, with rollback enabled). Use the target's Flash button at the [firmware center](https://firmware.hiphi.audio/) and **decline erase**: the NVS partition keeps its offset, so Wi-Fi and settings are retained. A downloaded merged factory image or an erase clears settings. Until a controller has had this reflash, it cannot take an OTA image and still needs USB. See [Firmware Flashing](FIRMWARE_FLASHING.md).

## Release Channels

| Channel | Installation | Update behavior |
|---------|--------------|-----------------|
| [Stable](https://firmware.hiphi.audio/stable/) | USB reflash once, then OTA through UHC | The bridge uses GitHub's `releases/latest`, which excludes prereleases. |
| [Beta](https://firmware.hiphi.audio/beta/) | Opt-in USB, then OTA through UHC | A beta or rc build follows the Beta channel. |
| [Alpha](https://firmware.hiphi.audio/alpha/) | Opt-in USB, then OTA through UHC | An alpha or dev build follows the Alpha channel. |

A prerelease follows its **own** channel. The controller derives the channel from the version it is running (`-beta*` and `-rc*` are Beta, any other prerelease suffix is Alpha, no suffix is Stable) and sends it as `?channel=` on both the version check and the download. A device on Beta never asks the Stable feed and is never moved to Alpha or Stable by an update. Automatic background checks still skip dev, alpha and beta builds; a bridge-requested install and the Dial's **Check for Update** force the check. Either way the version comparison must find a strictly newer version, otherwise nothing is written.

## Update a controller

From UHC:

1. Confirm the controller is online in UHC.
2. Press **Update** on its device page.
3. Leave the controller powered and on Wi-Fi while it fetches its configuration, downloads, validates and reboots.
4. After the reboot, check the firmware version shown in UHC, room, transport, volume and reconnection.

On the Dial:

1. Confirm the Dial is connected to Wi-Fi and UHC.
2. Open Settings and choose **Check for Update** if no available update is shown.
3. Review the offered version and start the update.
4. Keep the controller powered and connected while the image downloads and validates.

OTA writes the other application slot and keeps the NVS settings partition.

## Rollback and boot confirmation

Rollback is enabled in every build. After an OTA the bootloader runs the new image as "pending verification". The image confirms itself only after it has stayed up for **45 seconds**; the confirmation does not depend on Wi-Fi or the bridge. A crash or watchdog reset inside that window leaves the image unconfirmed and the bootloader returns to the previous slot. This is implemented from the ESP-IDF rollback API; no rollback has been triggered on hardware yet.

## What the Firmware Checks

The controller sends its identity (`X-Knob-Id`, `X-Knob-Version`, `X-Device-Type`) with the version and download requests. A bridge request names the version the controller reported when it was made. If that no longer equals the running version, the request is stale (the controller already updated) and is ignored; a request is also ignored while another OTA job runs. Before erasing a slot, the firmware checks the image's chip, flash size and application project name against its own running image, so a sibling target's binary is rejected. This catches accidents; it does not authenticate firmware. The ESP-IDF OTA APIs validate the completed image before it becomes the boot target.

A download or validation failure leaves the current firmware selected and does not reboot. A successful transfer does not prove sustained boot or hardware behavior; test those after each update.

Implementation: [ota_update.c](../../common/ota_update.c), [ota_update.h](../../common/ota_update.h), [controller_device_settings.c](../../common/controller_device_settings.c) (request parsing), and the Dial's [ui_network.c](../../idf_app/main/ui_network.c).

## Dial Partition Layout

This table describes the current Dial build only. Every target now has the same three-slot shape, but offsets and sizes are per target; read each target's `partitions.csv`. Do not assume an older release has the same slot sizes.

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

The firmware sends `X-Device-Type` (the device slug) and `?channel=` so a bridge can serve the matching target and channel. The firmware side does not depend on filenames, but whether a given UHC release selects the right per-target asset is a bridge matter this repository cannot verify: confirm it against the UHC version you run. Keep the `roon_knob.bin` compatibility alias (byte-identical to **hiphi_dial.bin**) available during the naming transition; [UHC issue #277](https://github.com/open-horizon-labs/unified-hifi-control/issues/277) tracks selecting the primary HiPhi asset.

The bridge may also add `firmware_update` (`{"action":"install","from_version":"<version>"}`) to a controller's `GET /config/<knob_id>` response while an install is pending, and optional device settings keys (`show_ip`, `brightness`, `body_enabled`, `sound_enabled`, `sound_volume`). Absent or invalid values are ignored.

The bridge downloads firmware for the requested channel from GitHub Releases. It does not use the web installer pages as an OTA feed. See the [UHC firmware service](https://github.com/open-horizon-labs/unified-hifi-control/blob/main/src/firmware.rs) for the current server implementation.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No update available | Confirm UHC has a newer Stable application image. A prerelease or a channel page alone does not enter the OTA feed. |
| UHC Update does nothing | The controller has not had the one-time partition reflash, is offline, or already runs the offered version (a stale request is ignored). It acts on the next configuration fetch, not instantly. |
| No bridge configured or connection failed | Restore the controller's connection to the selected UHC bridge before retrying. |
| Download or validation failed | Leave the current firmware running, inspect the offered release and bridge logs, and retry only after resolving the cause. |
| Wrong target or unsupported image | Use the exact target’s USB installer. Never rename another controller’s binary to the Dial compatibility filename. |
| Need to test a prerelease | Choose Beta or Alpha explicitly at the firmware center and install over USB once; later updates follow that channel. |

## Release Maintenance

The firmware workflow builds with the repository’s pinned ESP-IDF version, publishes application and component assets plus compatibility aliases, and updates the matching `/stable/`, `/beta/`, or `/alpha/` installer. See [Development](../dev/DEVELOPMENT.md) and the release workflow for the current process. Release promotion requires the maintainer's approval and physical checks of the exact artifact on its target hardware.
