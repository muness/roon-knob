# NVS Storage

This document covers how the Roon Knob persists configuration using ESP-IDF's Non-Volatile Storage (NVS).

## Overview

NVS is a key-value store built into ESP-IDF that persists data to flash memory. The Roon Knob uses it to store:

- WiFi credentials (SSID, password)
- Bridge URL (optional, mDNS used if empty)
- Selected Roon zone ID
- Configuration version

## Storage Schema

### Namespace and Key

| Parameter | Value |
|-----------|-------|
| Namespace | `rk_cfg` |
| Key | `cfg` |
| Type | Blob (binary) |

All configuration is stored as a single binary blob under one key. This simplifies versioning and atomic updates.

### Configuration Structure

```c
#define RK_CFG_CURRENT_VER 1

typedef struct {
    char ssid[33];           // WiFi SSID (32 chars + null)
    char pass[65];           // WiFi password (64 chars + null)
    char bridge_base[128];   // Bridge URL, e.g., "http://192.168.1.100:8088"
    char zone_id[64];        // Roon zone ID
    uint8_t cfg_ver;         // Config version for migrations
} rk_cfg_t;

// Total size: 291 bytes (enforced by static_assert)
```

The struct size is checked at compile time to catch accidental changes that would break stored data.

### Field Details

| Field | Size | Description |
|-------|------|-------------|
| `ssid` | 33 bytes | WiFi network name (max 32 chars) |
| `pass` | 65 bytes | WiFi password (max 64 chars) |
| `bridge_base` | 128 bytes | HTTP URL to bridge, empty = use mDNS |
| `zone_id` | 64 bytes | Roon zone identifier |
| `knob_name` | 32 bytes | Device name for DHCP/mDNS hostname (optional, falls back to MAC-based) |
| `cfg_ver` | 1 byte | Schema version for future migrations |

**Hostname behavior:**
- `knob_name` is sanitized to RFC 1123 (lowercase alphanumeric + hyphens)
- If empty, defaults to MAC-based name (`roon-knob-a1b2c3`)
- Hostname changes require reboot (cache invalidated on boot)

## API Reference

### platform_storage.h

```c
// Load configuration from NVS
// Returns: true if loaded successfully, false if not found or error
bool platform_storage_load(rk_cfg_t *out);

// Save configuration to NVS
// Returns: true if saved successfully
bool platform_storage_save(const rk_cfg_t *in);

// Apply default values to config struct
// Clears all fields, sets cfg_ver to current version
void platform_storage_defaults(rk_cfg_t *out);

// Clear WiFi credentials only (preserves bridge/zone)
// Saves immediately after clearing
void platform_storage_reset_wifi_only(rk_cfg_t *cfg);
```

## Load/Save Flow

### Loading

```c
rk_cfg_t cfg = {0};
if (platform_storage_load(&cfg)) {
    // Config loaded from NVS
    // All fields populated
} else {
    // No config in NVS, or error
    // cfg is zeroed
    platform_storage_defaults(&cfg);
    platform_storage_save(&cfg);
}
```

### Saving

```c
cfg.zone_id[0] = '\0';  // Clear zone selection
if (platform_storage_save(&cfg)) {
    // Saved to flash
} else {
    // Write failed (flash full, etc.)
}
```

## Configuration Validation

```c
static inline bool rk_cfg_is_valid(const rk_cfg_t *cfg) {
    return cfg && cfg->cfg_ver != 0 && cfg->bridge_base[0] != '\0';
}
```

A config is "valid" when:
1. Pointer is not NULL
2. Version is non-zero (has been configured)
3. Bridge URL is set

Note: WiFi credentials are validated separately by the WiFi manager.

## Default Values

When no configuration exists:

| Field | Default | Source |
|-------|---------|--------|
| `ssid` | `""` | Kconfig `CONFIG_RK_DEFAULT_SSID` (applied by wifi_manager) |
| `pass` | `""` | Kconfig `CONFIG_RK_DEFAULT_PASS` (applied by wifi_manager) |
| `bridge_base` | `""` | Empty - discovered via mDNS |
| `zone_id` | `""` | Empty - user selects from zone picker |
| `cfg_ver` | `1` | Current schema version |

## Reset Triggers

### Full Reset (Factory Reset)

NVS is erased when:

1. **Partition corrupted** - `ESP_ERR_NVS_NO_FREE_PAGES`
2. **Version mismatch** - `ESP_ERR_NVS_NEW_VERSION_FOUND` (IDF upgrade)

```c
// In app_main()
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
    err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();  // Wipe entire NVS partition
    nvs_flash_init();   // Reinitialize
}
```

After a full reset, all settings (WiFi, bridge, zone) are lost.

### WiFi-Only Reset

```c
platform_storage_reset_wifi_only(&cfg);
```

Clears only `ssid` and `pass`, preserving `bridge_base` and `zone_id`. Used by the "Forget WiFi" settings option.

This triggers:
1. WiFi manager sees empty SSID
2. Switches to AP mode for reprovisioning
3. Bridge and zone preserved for when WiFi reconnects

## Versioning and Migrations

The `cfg_ver` field enables future schema changes:

```c
#define RK_CFG_CURRENT_VER 1

// Future migration example:
bool platform_storage_load(rk_cfg_t *out) {
    // ... load blob ...

    // Migrate from v1 to v2
    if (out->cfg_ver == 1) {
        // Add new field defaults
        out->new_field = default_value;
        out->cfg_ver = 2;
        platform_storage_save(out);
    }

    return true;
}
```

Currently at version 1 - no migrations needed yet.

## Flash Wear Considerations

NVS uses wear leveling, but excessive writes should be avoided:

| Operation | Frequency | Notes |
|-----------|-----------|-------|
| Save WiFi credentials | Once per provisioning | Rare |
| Save zone selection | When user changes zone | Occasional |
| Save bridge URL | When manually configured | Rare |

The device does NOT save on every volume change or playback state - those are transient and not persisted.

## Error Handling

### Load Errors

```c
if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Normal: first boot or after reset
    // Return false, let caller apply defaults
}
if (err != ESP_OK) {
    // Unexpected error (flash issue, etc.)
    ESP_LOGW(TAG, "nvs read failed: %s", esp_err_to_name(err));
    memset(out, 0, sizeof(*out));
    return false;
}
```

### Save Errors

```c
if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs save failed: %s", esp_err_to_name(err));
    return false;
}
```

Common save errors:
- `ESP_ERR_NVS_NOT_ENOUGH_SPACE` - NVS partition full
- `ESP_ERR_NVS_INVALID_HANDLE` - Namespace not opened

## Partition Layout

NVS uses a dedicated flash partition:

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| nvs | 0x9000 | 16 KB | Key-value storage |

The 16KB partition is far larger than needed for the ~300 byte config blob, leaving room for future expansion.

## Usage Examples

### First Boot

```c
rk_cfg_t cfg = {0};
if (!platform_storage_load(&cfg)) {
    // First boot - no config saved
    platform_storage_defaults(&cfg);
    // WiFi manager will apply Kconfig SSID/pass defaults
}
```

### After WiFi Provisioning

```c
// In captive portal handler
rk_cfg_t cfg = {0};
platform_storage_load(&cfg);  // Get existing config

strncpy(cfg.ssid, user_ssid, sizeof(cfg.ssid) - 1);
strncpy(cfg.pass, user_pass, sizeof(cfg.pass) - 1);
cfg.cfg_ver = 1;

platform_storage_save(&cfg);  // Persist new credentials
wifi_mgr_reconnect(&cfg);     // Apply immediately
```

### Zone Selection

```c
void on_zone_selected(const char *zone_id) {
    rk_cfg_t cfg = {0};
    platform_storage_load(&cfg);

    strncpy(cfg.zone_id, zone_id, sizeof(cfg.zone_id) - 1);
    platform_storage_save(&cfg);

    bridge_client_set_zone(zone_id);
}
```

## Implementation Files

| File | Purpose |
|------|---------|
| `common/rk_cfg.h` | Configuration structure definition |
| `common/platform/platform_storage.h` | Platform-agnostic API |
| `idf_app/main/platform_storage_idf.c` | ESP-IDF NVS implementation |
| `pc_sim/platform_storage_pc.c` | PC simulator (JSON file storage) |
