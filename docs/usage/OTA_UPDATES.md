# OTA Updates

Over-the-air firmware updates allow the Roon Knob to download and install new firmware directly from the bridge without USB flashing.

## Overview

The OTA system consists of:

- **Device firmware** (`idf_app/main/ota_update.c`) - checks for updates, downloads, and installs
- **Bridge server** (`roon-extension/routes.js`) - serves version info and firmware binary
- **CI/CD pipeline** (`.github/workflows/docker.yml`) - builds and packages firmware on release

## Partition Layout

The ESP32-S3 uses a dual-OTA partition scheme for safe updates:

| Partition | Type    | Offset     | Size    | Purpose |
|-----------|---------|------------|---------|---------|
| nvs       | data    | 0x9000     | 16 KB   | Non-volatile storage |
| otadata   | data    | 0xd000     | 8 KB    | OTA state tracking |
| phy_init  | data    | 0xf000     | 4 KB    | PHY calibration |
| factory   | app     | 0x10000    | 2 MB    | Factory firmware |
| ota_0     | app     | 0x210000   | 2 MB    | OTA slot A |
| ota_1     | app     | 0x410000   | 2 MB    | OTA slot B |
| spiffs    | data    | 0x610000   | ~10 MB  | File storage |

Source: `idf_app/partitions.csv`

The ESP-IDF OTA APIs automatically alternate between `ota_0` and `ota_1`, ensuring the device can always roll back if an update fails validation.

## ESP-IDF OTA API

The firmware uses ESP-IDF's native OTA API (`esp_ota_ops.h`) which provides a robust, battle-tested implementation for over-the-air updates. Here's how the key functions are used:

### Partition Selection

```c
const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
```

`esp_ota_get_next_update_partition()` automatically determines which OTA partition to write to. Passing `NULL` tells it to select the next partition in the A/B rotation based on the current boot partition. This ensures updates alternate between `ota_0` and `ota_1`.

### OTA Write Session

The OTA process uses a handle-based API that manages flash writes:

```c
esp_ota_handle_t ota_handle;
esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
```

`esp_ota_begin()` prepares the partition for writing by:
- Erasing the target partition
- Initializing internal state for incremental writes
- Returning a handle for subsequent operations

The `OTA_SIZE_UNKNOWN` constant tells the API that we'll write data in chunks without knowing the exact size upfront (the size is validated at the end).

### Incremental Flash Writes

```c
err = esp_ota_write(ota_handle, buf, read_len);
```

`esp_ota_write()` writes data to flash in chunks. The implementation downloads firmware in 4KB chunks and writes each chunk immediately. This streaming approach minimizes RAM usage - the device never needs to hold the entire firmware image in memory.

### Validation and Finalization

```c
err = esp_ota_end(ota_handle);
```

`esp_ota_end()` performs critical validation:
- Verifies the image header and magic bytes
- Checks the application descriptor
- Validates the image checksum
- Ensures the image is properly formatted for the target chip

If validation fails, the partition is marked invalid and the device continues booting from the current partition.

### Setting Boot Partition

```c
err = esp_ota_set_boot_partition(update_partition);
```

`esp_ota_set_boot_partition()` updates the `otadata` partition to mark the newly written partition as the boot target. On next reboot, the bootloader will load firmware from this partition.

### Version Information

```c
const esp_app_desc_t *app_desc = esp_app_get_description();
const char *version = app_desc->version;
```

`esp_app_get_description()` returns metadata embedded in the firmware binary at build time, including the version string set via `PROJECT_VER` in CMakeLists.txt.

### Error Recovery

If an OTA operation fails mid-stream, `esp_ota_abort()` cleans up resources and marks the partition as invalid:

```c
esp_ota_abort(ota_handle);
```

The dual-partition scheme ensures the device always has a working firmware to fall back to - a failed update simply leaves the current partition as the boot target.

## Server-Side Setup

### Firmware Directory Structure

The bridge serves firmware from `roon-extension/firmware/`:

```
roon-extension/
└── firmware/
    ├── version.json    # Version metadata
    └── roon_knob.bin   # Firmware binary
```

### version.json Format

```json
{
  "version": "1.2.12",
  "file": "roon_knob.bin"
}
```

The `size` field is computed at runtime from the binary file.

### Fallback Behavior

If `version.json` is missing, the server:

1. Looks for `.bin` files in the firmware directory
2. Uses the first `.bin` file found
3. Attempts to extract version from filename pattern: `roon_knob[_-]?v?(\d+\.\d+\.\d+)\.bin`

## API Reference

### GET /firmware/version

Returns available firmware version and size.

**Response (200 OK):**

```json
{
  "version": "1.2.12",
  "size": 1536000,
  "file": "roon_knob.bin"
}
```

**Response (404 Not Found):**

```json
{
  "error": "No firmware available"
}
```

Returns 404 when:
- Firmware directory doesn't exist
- No `.bin` files present
- Version cannot be determined

### GET /firmware/download

Streams the firmware binary.

**Response Headers:**

```
Content-Type: application/octet-stream
Content-Length: 1536000
Content-Disposition: attachment; filename="roon_knob.bin"
```

**Response (404 Not Found):**

```json
{
  "error": "Firmware file not found"
}
```

## Device-Side Flow

### Check for Update

```
ota_check_for_update()
    └── spawns check_update_task
        ├── Status = OTA_STATUS_CHECKING
        ├── GET /firmware/version
        ├── Parse JSON for "version" and "size"
        ├── Compare with current version (semver)
        └── Status = OTA_STATUS_AVAILABLE or OTA_STATUS_UP_TO_DATE
```

### Perform Update

```
ota_start_update()
    └── spawns do_update_task
        ├── Status = OTA_STATUS_DOWNLOADING
        ├── esp_ota_get_next_update_partition(NULL) - select target partition
        ├── esp_ota_begin() - erase partition and prepare for writes
        ├── GET /firmware/download
        │   └── Download in 4KB chunks
        │       └── esp_ota_write() for each chunk
        ├── esp_ota_end() - validate image checksum and format
        ├── esp_ota_set_boot_partition() - mark new partition as boot target
        ├── Status = OTA_STATUS_COMPLETE
        └── Reboot after 2 seconds
```

## Version Comparison

Versions use semantic versioning (major.minor.patch). Examples:
- `1.2.12` > `1.2.11` (patch higher)
- `1.3.0` > `1.2.12` (minor higher)
- `v2.0.0` > `1.9.9` (major higher, `v` prefix ignored)

## Error Handling

### Status Enum

| Status | Description |
|--------|-------------|
| `OTA_STATUS_IDLE` | No OTA operation in progress |
| `OTA_STATUS_CHECKING` | Checking for available update |
| `OTA_STATUS_AVAILABLE` | Newer version available |
| `OTA_STATUS_DOWNLOADING` | Download in progress |
| `OTA_STATUS_COMPLETE` | Update complete, rebooting |
| `OTA_STATUS_ERROR` | Operation failed |
| `OTA_STATUS_UP_TO_DATE` | Already running latest version |

### Error Messages

Error details are stored in `ota_info_t.error_msg` (64 characters max):

| Error Message | Cause |
|---------------|-------|
| `No bridge configured` | Bridge URL not set in device storage |
| `Connection failed` | HTTP connection to bridge failed |
| `Bad server response` | Non-200 status or invalid content length |
| `Read failed` | Failed to read HTTP response body |
| `Missing version` | JSON response missing "version" field |
| `Invalid JSON` | Malformed JSON in version response |
| `Invalid version` | Version string too long or malformed |
| `No OTA partition` | Partition table misconfigured |
| `OTA begin failed` | Failed to initialize OTA write (partition erase failed) |
| `Out of memory` | Could not allocate download buffer |
| `Write failed` | Flash write error during download |
| `Download incomplete` | Received fewer bytes than expected |
| `Validation failed` | Image failed ESP-IDF validation (bad checksum, wrong chip, etc.) |
| `Set boot failed` | Could not set new boot partition |

## User Flow

1. Device periodically calls `ota_check_for_update()`
2. If update available, UI shows green "Update to vX.Y.Z" notification
3. User taps notification
4. `ui_trigger_update()` calls `ota_start_update()`
5. Progress bar shows download progress
6. On completion, device reboots to new firmware

## Release Process

### Creating a Release

```bash
./scripts/release_firmware.sh 1.2.13
```

This script:

1. Updates `PROJECT_VER` in `idf_app/CMakeLists.txt`
2. Commits the version change
3. Creates and pushes git tag `v1.2.13`

### CI/CD Pipeline

On tag push (`v*`), GitHub Actions (`.github/workflows/docker.yml`):

1. Builds firmware with ESP-IDF 5.4
2. Creates `version.json` with tag version
3. Creates GitHub Release with `.bin` attached
4. Builds Docker image `muness/roon-extension-knob` with firmware baked in

The Docker image serves as the distribution mechanism - users running the bridge container automatically have the latest firmware available for OTA.
