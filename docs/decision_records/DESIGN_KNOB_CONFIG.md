# Design: Per-Knob Configuration from Bridge

**Status:** Approved
**Date:** 2025-12-20

## Summary

Bridge-managed per-knob configuration for display rotation, dim/sleep behavior, zone filtering, and knob naming.

## Decisions

| Topic            | Decision                                                                              |
| ---------------- | ------------------------------------------------------------------------------------- |
| Config retrieval | New `/config/{knob_id}` endpoint + `config_sha` in `/now_playing` for change detection |
| Rotation         | Two values: `rotation_charging` and `rotation_not_charging`. Defaults: 180°, 0°         |
| Zone filtering   | Three modes: `all`, `include`, `exclude` (zones come/go, all are useful)                |
| Knob naming      | Free text, displayed during boot + in settings                                          |
| Fallback         | NVS cache first, then compile-time defaults                                             |
| Config UI        | Bridge admin dashboard only (not on-device "advanced config")                           |

### Rationale

**Config retrieval via dedicated endpoint + SHA polling:** The knob already polls `/now_playing` every second. Adding a `config_sha` field there gives us pseudo-push behavior with zero extra HTTP requests. When SHA changes, the knob fetches full config from `/config/{knob_id}`. This is simpler than WebSocket and leverages existing infrastructure.

**Two rotation values (charging vs battery):** USB-powered desk setups often want 180° rotation (cable from top). Battery/portable use may want 0°. Two separate values let users optimize for both scenarios without compromise. Defaults (180° charging, 0° battery) match the common "USB on desk" use case.

**Three zone filter modes:** Zones come and go (Roon groups, Bluetooth virtual zone). Include-list is precise but breaks when zones disappear. Exclude-list is resilient but verbose. Supporting all three lets users pick what fits their setup. "All" is the safe default.

**Bridge-only config UI:** User explicitly said "I don't really want to do advanced config on the knob." Keeping config in the bridge admin dashboard simplifies the firmware and provides a better editing experience (larger screen, keyboard input for names).

**NVS cache fallback:** If bridge is unreachable at boot, using cached config provides continuity. Compile-time defaults are last resort for fresh devices.

## Config Schema

```typescript
interface KnobConfig {
  knob_id: string;              // MAC address hex (e.g., "a1b2c3d4e5f6")
  name: string;                 // User-friendly label

  // Rotation (degrees)
  rotation_charging: 0 | 90 | 180 | 270;      // Default: 180
  rotation_not_charging: 0 | 90 | 180 | 270;  // Default: 0

  // Dimming
  dim_charging: {
    enabled: boolean;           // false = never dim when charging
    timeout_sec: number;        // seconds before dimming
  };
  dim_battery: {
    enabled: boolean;
    timeout_sec: number;
  };

  // Sleep
  sleep_charging: {
    enabled: boolean;           // false = never sleep when charging
    timeout_sec: number;        // seconds after dim before sleep
  };
  sleep_battery: {
    enabled: boolean;
    timeout_sec: number;
  };

  // Zone filtering (includes Bluetooth virtual zone)
  zones: {
    mode: "all" | "include" | "exclude";
    zone_ids: string[];         // zone IDs to include/exclude
  };
}
```

### Defaults (New Knob)

```json
{
  "name": "",
  "rotation_charging": 180,
  "rotation_not_charging": 0,
  "dim_charging": { "enabled": true, "timeout_sec": 30 },
  "dim_battery": { "enabled": true, "timeout_sec": 30 },
  "sleep_charging": { "enabled": true, "timeout_sec": 60 },
  "sleep_battery": { "enabled": true, "timeout_sec": 60 },
  "zones": { "mode": "all", "zone_ids": [] }
}
```

## API Design

### GET /config/:knob_id

Fetch full config for a knob. Creates default config if knob_id not seen before.

**Response:**
```json
{
  "config": { /* KnobConfig */ },
  "config_sha": "a1b2c3d4"
}
```

### PUT /config/:knob_id

Update config (from admin UI). Partial updates allowed.

### GET /now_playing (existing, extended)

Add `config_sha` field to response:

```json
{
  "line1": "Track Name",
  "line2": "Artist",
  "is_playing": true,
  "config_sha": "a1b2c3d4"
}
```

Knob stores last-known SHA. When it changes, fetch full config.

### GET /zones (existing, modified)

Apply zone filtering server-side based on knob's config. Knob already sends `X-Knob-Id` header.

### GET /knobs

List all known knobs (for admin UI):

```json
{
  "knobs": [
    {
      "knob_id": "a1b2c3d4e5f6",
      "name": "Office Knob",
      "last_seen": "2025-12-20T10:30:00Z",
      "version": "1.3.1"
    }
  ]
}
```

## Firmware Flow

```text
Boot
  │
  ├─→ Display knob name (if cached) during boot screen
  │
  ├─→ Connect WiFi → Discover bridge
  │
  ├─→ GET /config/{knob_id}
  │     ├─→ Success: Apply config, cache in NVS, store SHA
  │     └─→ Failure: Use NVS cache, or compile-time defaults
  │
  └─→ Main loop
        │
        ├─→ GET /now_playing (every 1s when awake)
        │     └─→ If config_sha differs: GET /config/{knob_id} (with lock)
        │
        └─→ On charging state change: re-apply rotation + dim/sleep settings
```

## Storage

### Bridge: `data/knobs.json`

```json
{
  "a1b2c3d4e5f6": {
    "name": "Office Knob",
    "last_seen": "2025-12-20T10:30:00Z",
    "version": "1.3.1",
    "config": { /* KnobConfig */ },
    "config_sha": "a1b2c3d4"
  }
}
```

SHA computed from JSON.stringify(config) → first 8 chars of SHA256.

### Knob: NVS extension

Extend `rk_cfg_t` to cache display settings + SHA:

```c
typedef struct {
    // Existing
    char ssid[33];
    char pass[65];
    char bridge_base[128];
    char zone_id[64];

    // NEW: Cached config
    char knob_name[32];
    char config_sha[9];          // 8 hex chars + null
    uint16_t rotation_charging;  // 0, 90, 180, 270
    uint16_t rotation_not_charging;
    uint8_t  dim_charging_enabled;
    uint16_t dim_charging_timeout_sec;
    uint8_t  dim_battery_enabled;
    uint16_t dim_battery_timeout_sec;
    uint8_t  sleep_charging_enabled;
    uint16_t sleep_charging_timeout_sec;
    uint8_t  sleep_battery_enabled;
    uint16_t sleep_battery_timeout_sec;
    // Zone filtering stored separately or not cached (fetch on boot)

    uint8_t cfg_ver;  // Bump to 2
} rk_cfg_t;
```

## Implementation Phases

### Phase 1: Bridge API + Storage
- Add `data/knobs.json` storage layer
- Implement `/config/:knob_id` GET/PUT
- Implement `/knobs` GET
- Add `config_sha` to `/now_playing` response
- Modify `/zones` to apply zone filtering

### Phase 2: Firmware Config Fetch
- Extend `rk_cfg_t` with display settings
- Fetch config on boot
- Apply rotation via LVGL `lv_display_set_rotation()`
- Wire dim/sleep to `display_sleep.c`
- Display knob name during boot + in settings

### Phase 3: Change Detection
- Track `config_sha` in `/now_playing` polling
- Fetch new config when SHA changes (with mutex)
- Re-apply settings on charging state change

### Phase 4: Admin UI
- `/admin/knobs` page listing known knobs
- Knob edit page with all config options
- Zone multi-select from Roon zones list (including Bluetooth virtual zone)

## Charging Detection

Prerequisite for Phase 3. Options:
- GPIO connected to charger IC status pin
- ADC voltage threshold (VBUS presence)
- For USB-only setups: assume always charging

This is covered by existing beads task `roon-knob-13q`.
