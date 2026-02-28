# Kconfig Options

This document covers the build-time configuration options available for the Roon Knob firmware.

## Overview

ESP-IDF uses Kconfig for compile-time configuration. Options are defined in `Kconfig.projbuild` and defaults are set in `sdkconfig.defaults`. After changing options, rebuild the firmware to apply them.

## Project-Specific Options

### Roon-Knob Defaults Menu

Options for development and testing. In production, these are typically left empty (WiFi is configured via captive portal, bridge via mDNS).

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_RK_DEFAULT_SSID` | string | `""` | Pre-configured WiFi SSID |
| `CONFIG_RK_DEFAULT_PASS` | string | `""` | Pre-configured WiFi password |
| `CONFIG_RK_DEFAULT_BRIDGE_BASE` | string | `""` | Bridge URL fallback if mDNS fails |

**Usage:** Set these during development to skip the provisioning flow:

```bash
idf.py menuconfig
# Navigate to: Roon-Knob Defaults
# Set your home WiFi credentials
```

### Display Sleep Settings Menu

Controls display power management behavior.

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `CONFIG_RK_DISPLAY_DIM_TIMEOUT_SEC` | int | 30 | 5-300 | Seconds before dimming |
| `CONFIG_RK_DISPLAY_SLEEP_TIMEOUT_SEC` | int | 60 | 10-600 | Seconds before sleep |
| `CONFIG_RK_BACKLIGHT_NORMAL` | int | 100 | 0-255 | Normal brightness (~40%) |
| `CONFIG_RK_BACKLIGHT_DIM` | int | 25 | 0-255 | Dimmed brightness (~10%) |

**Timeline:**

```
User activity → [30s idle] → Dim → [30s more] → Sleep
                              ↑                    ↑
                        (60-30=30s)           (total 60s)
```

**Brightness scale:** 0-255 maps to PWM duty cycle. Perception is non-linear:
- 25 (~10%) is visible but very dim
- 100 (~40%) is comfortable for indoor use
- 255 (100%) is maximum, may be too bright

## ESP-IDF Options (sdkconfig.defaults)

### Flash Configuration

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
```

The ESP32-S3-Knob has 16MB flash. This enables the full partition layout including OTA slots.

### PSRAM Configuration

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
```

| Option | Purpose |
|--------|---------|
| `SPIRAM=y` | Enable 8MB PSRAM |
| `MODE_OCT` | Octal SPI mode (faster) |
| `SPEED_80M` | 80MHz clock |
| `USE_MALLOC` | Include PSRAM in heap |
| `ALWAYSINTERNAL=16384` | Allocations <16KB use internal RAM |
| `TRY_ALLOCATE_WIFI_LWIP` | Let WiFi/TCP use PSRAM |

PSRAM is used for large buffers like album artwork (360×360×2 = 259KB).

### Brownout Detection

```
CONFIG_ESP_BROWNOUT_DET=y
CONFIG_ESP_BROWNOUT_DET_LVL_SEL_4=y
CONFIG_ESP_BROWNOUT_DET_LVL=4
```

**Why Level 4 (2.50V)?**

The default Level 7 (2.80V) triggers false brownouts during WiFi transmission on battery power. WiFi causes ~500mA current spikes that momentarily drop voltage. Level 4 tolerates these transients.

| Level | Voltage | Use Case |
|-------|---------|----------|
| 7 | 2.80V | USB-powered only |
| 4 | 2.50V | Battery operation ✓ |
| 1 | 2.36V | Aggressive, risk of instability |

### LVGL Fonts

```
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_18=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_22=y
CONFIG_LV_FONT_MONTSERRAT_24=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_30=y
CONFIG_LV_FONT_MONTSERRAT_32=y
CONFIG_LV_FONT_MONTSERRAT_48=y
```

Enables various Montserrat font sizes for the UI. Each adds ~10-20KB to the binary. The 360×360 display uses larger fonts (20-48pt) for readability.

### HTTP Server

```
CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
```

Default 1024 bytes is insufficient for modern browsers that send many headers. 2048 bytes accommodates typical browser requests to the captive portal.

### Partition Table

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

Uses custom partition layout for dual-OTA support. See [OTA_UPDATES.md](../usage/OTA_UPDATES.md) for partition details.

## Changing Options

**IMPORTANT:** The `sdkconfig` file (generated) takes precedence over `sdkconfig.defaults`. If you add new options to `sdkconfig.defaults`, you MUST delete sdkconfig to apply them:

```bash
# After editing sdkconfig.defaults:
rm sdkconfig
idf.py build
```

Simply running `idf.py reconfigure` or `idf.py fullclean` is NOT sufficient—the sdkconfig file must be deleted.

### Via menuconfig (Interactive)

```bash
cd esp_dial
idf.py menuconfig
# Navigate menus, change values
# Save and exit
idf.py build
```

### Via sdkconfig.defaults (Persistent)

Edit `esp_dial/sdkconfig.defaults` and rebuild:

```bash
# Add or modify options
echo "CONFIG_RK_DISPLAY_DIM_TIMEOUT_SEC=60" >> sdkconfig.defaults

# Clean and rebuild to apply
idf.py fullclean
idf.py build
```

### Via Command Line (Temporary)

```bash
idf.py -D CONFIG_RK_DEFAULT_SSID="MyWiFi" build
```

Note: Command-line overrides don't persist to `sdkconfig`.

## Option Precedence

1. **sdkconfig** (generated, highest priority)
2. **sdkconfig.defaults** (project defaults)
3. **Kconfig defaults** (lowest priority)

After `idf.py menuconfig`, changes are saved to `sdkconfig`. To reset to defaults:

```bash
rm sdkconfig
idf.py reconfigure
```

## Development vs Production

### Development Build

```
# sdkconfig.defaults.dev (create this file)
CONFIG_RK_DEFAULT_SSID="MyHomeWiFi"
CONFIG_RK_DEFAULT_PASS="mypassword"
CONFIG_RK_DEFAULT_BRIDGE_BASE="http://192.168.1.100:8088"
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

```bash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.dev" build
```

### Production Build

Use only `sdkconfig.defaults` with empty WiFi credentials. Users configure via captive portal.

## Adding New Options

### 1. Define in Kconfig.projbuild

```kconfig
menu "My Feature"

config MY_FEATURE_ENABLED
    bool "Enable my feature"
    default y
    help
        Enable or disable my feature.

config MY_FEATURE_TIMEOUT
    int "Timeout in seconds"
    default 10
    range 1 60
    depends on MY_FEATURE_ENABLED

endmenu
```

### 2. Use in Code

```c
#include "sdkconfig.h"

#ifdef CONFIG_MY_FEATURE_ENABLED
void my_feature_init(void) {
    int timeout = CONFIG_MY_FEATURE_TIMEOUT;
    // ...
}
#endif
```

### 3. Set Defaults

```bash
# Add to sdkconfig.defaults
CONFIG_MY_FEATURE_ENABLED=y
CONFIG_MY_FEATURE_TIMEOUT=10
```

## Reference Files

| File | Purpose |
|------|---------|
| `esp_dial/main/Kconfig.projbuild` | Project-specific option definitions |
| `esp_dial/sdkconfig.defaults` | Default values for all builds |
| `esp_dial/sdkconfig` | Generated config (don't commit) |
