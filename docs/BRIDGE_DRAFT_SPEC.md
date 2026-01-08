# Bridge HTTP Protocol - DRAFT SPECIFICATION

> **⚠️ WORK IN PROGRESS - NOT A STABLE API**
> This specification documents the **current implementation** of the unified-hifi-control HTTP protocol as of January 2026.
> **This protocol is NOT stable and may change without notice.** Use at your own risk.
> See [Stability Notes](#stability-notes) below.

## Overview

The [unified-hifi-control](https://github.com/cloud-atlas-ai/unified-hifi-control) bridge exposes a simple HTTP REST API that clients (like the Roon Knob firmware) use to:
- Discover available playback zones across multiple music sources
- Query current playback state
- Send control commands (play/pause, volume, next/prev)
- Fetch album artwork

This bridges the gap between proprietary music server APIs (Roon, Lyrion Music Server/LMS, OpenHome) and simple HTTP clients running on resource-constrained devices like ESP32.

**Why a bridge?** See [Bridge Architecture Rationale](./meta/decisions/2025-12-22_DECISION_BRIDGE_ARCHITECTURE.md).

## Base URL

Default: `http://<bridge-host>:8088`

The bridge runs as a Docker container or Node.js service on your network. Clients discover it via mDNS (`_roonextknob._tcp`) or use a configured URL.

## Authentication

None currently. The bridge assumes a trusted local network.

## Endpoints

### 1. List Zones

Get all available playback zones from connected music servers.

```http
GET /zones?knob_id=<knob_id>
```

**Query Parameters:**
- `knob_id` (string, optional): Client identifier for telemetry

**Response (200 OK):**
```json
{
  "zones": [
    {
      "zone_id": "roon:15f84e4e2bfc07ea",
      "zone_name": "Living Room",
      "state": "playing",
      "output_count": 1,
      "output_name": "DAC",
      "volume_control": {
        "type": "number",
        "min": 0,
        "max": 100,
        "is_muted": false
      }
    },
    {
      "zone_id": "lms:a3:d8:2c:91:f4:b6",
      "zone_name": "Squeezebox Bedroom",
      "state": "stopped",
      "output_count": 1,
      "volume_control": {
        "type": "number",
        "min": 0,
        "max": 100,
        "is_muted": false
      }
    }
  ]
}
```

**Zone Fields:**
- `zone_id` (string): Prefixed zone identifier (e.g., `roon:`, `lms:`, `openhome:`)
- `zone_name` (string): Human-readable zone name
- `state` (string): `playing` | `paused` | `stopped` | `buffering`
- `output_count` (int): Number of outputs in zone
- `output_name` (string, optional): Name of primary output device
- `volume_control` (object | null): Volume capabilities, or null if not supported

**Notes:**
- Zone IDs include a prefix indicating the source (`roon:`, `lms:`, `openhome:`, `upnp:`)
- The `zones` array aggregates zones from all connected music servers
- Zone list may change dynamically as servers start/stop or devices connect/disconnect

---

### 2. Now Playing

Get current playback state for a zone.

```http
GET /now_playing?zone_id=<zone_id>&battery_level=<level>&battery_charging=<0|1>&knob_id=<knob_id>
```

**Query Parameters:**
- `zone_id` (string, required): Zone to query (with prefix, e.g., `roon:abc123`)
- `battery_level` (int, optional): Client battery percentage (0-100)
- `battery_charging` (int, optional): `1` if charging, `0` if on battery
- `knob_id` (string, optional): Client identifier for config hash lookup

**Response (200 OK):**
```json
{
  "line1": "Take Five",
  "line2": "Dave Brubeck Quartet - Time Out",
  "is_playing": true,
  "volume": 45,
  "volume_min": 0,
  "volume_max": 100,
  "volume_step": 1.0,
  "seek_position": 142,
  "length": 324,
  "image_key": "roon:abc123:image789",
  "image_url": "/now_playing/image?zone_id=roon%3Aabc123",
  "zones": [ /* same as /zones response */ ],
  "config_sha": "x7y9z2a5",
  "zones_sha": "a1b2c3d4"
}
```

**Fields:**
- `line1` (string): Primary display text (track title or "Idle")
- `line2` (string): Secondary display text (artist/album)
- `is_playing` (bool): True if playing, false if paused/stopped
- `volume` (float): Current volume (0-100 scale, or dB depending on backend)
- `volume_min` (float): Minimum volume limit
- `volume_max` (float): Maximum volume limit
- `volume_step` (float): Recommended volume step size (default: 1.0)
- `seek_position` (int): Current position in seconds (0 if unavailable)
- `length` (int): Track length in seconds (0 if unavailable)
- `image_key` (string): Opaque key for fetching artwork via `/now_playing/image`
- `image_url` (string): Relative URL to fetch artwork
- `zones` (array): Current zone list (same format as `GET /zones`)
- `config_sha` (string): 8-char hash for detecting config changes
- `zones_sha` (string): 8-char hash for detecting zone list changes

**Error Response (400):**
```json
{
  "error": "zone_id required",
  "zones": [ /* available zones */ ]
}
```

**Error Response (404):**
```json
{
  "error": "zone not found",
  "zones": [ /* available zones */ ]
}
```

**Notes:**
- Clients should poll this endpoint periodically (2-5s active, 30-60s idle)
- Battery parameters are logged but not currently used for features
- The `config_sha` notifies clients when device-specific config has changed
- The `zones_sha` notifies clients when the zone list has changed (zones added/removed)
- Zones array is included for convenience (avoids separate `/zones` request)

---

### 3. Album Artwork

Fetch album artwork for the current track.

```http
GET /now_playing/image?zone_id=<zone_id>&width=<w>&height=<h>&format=<format>
```

**Query Parameters:**
- `zone_id` (string, required): Zone ID (with prefix)
- `width` (int, optional): Desired width in pixels (default: 360)
- `height` (int, optional): Desired height in pixels (default: 360)
- `format` (string, optional): `jpeg` (default) or `rgb565` for ESP32 displays

**Response (200 OK) - JPEG:**
- Content-Type: `image/jpeg`
- Body: Resized JPEG image data

**Response (200 OK) - RGB565:**
- Content-Type: `application/octet-stream`
- Headers:
  - `X-Image-Width`: Image width in pixels
  - `X-Image-Height`: Image height in pixels
  - `X-Image-Format`: `rgb565`
- Body: Raw RGB565 pixel data (little-endian, 16-bit per pixel)

**Response (200 OK) - No Artwork:**
- Content-Type: `image/svg+xml`
- Body: Placeholder SVG with "No Image" text

**Error Response (400):**
```json
{
  "error": "zone_id required"
}
```

**Error Response (404):**
```json
{
  "error": "zone not found"
}
```

**Notes:**
- The bridge fetches artwork from the backend (Roon, LMS, etc.) and resizes it
- `rgb565` format is optimized for ESP32 displays (no on-device conversion needed)
- RGB565 pixel format: `RRRRRGGGGGGBBBBB` (5-6-5 bits), little-endian byte order
- Clients should cache images by `image_key` from `/now_playing` to avoid redundant fetches
- If artwork fetch fails, a placeholder SVG is returned (still 200 OK)

---

### 4. Control

Send playback control commands to a zone.

```http
POST /control
Content-Type: application/json
```

**Request Body:**
```json
{
  "zone_id": "roon:15f84e4e2bfc07ea",
  "action": "play_pause"
}
```

**Actions:**

| Action | Description | Additional Fields |
|--------|-------------|-------------------|
| `play` | Start playback | - |
| `pause` | Pause playback | - |
| `play_pause` | Toggle play/pause | - |
| `stop` | Stop playback | - |
| `next` | Skip to next track | - |
| `prev` / `previous` | Skip to previous track | - |
| `vol_abs` | Set absolute volume | `value` (float): Volume level |
| `vol_rel` | Adjust volume relatively | `value` (float): Delta |

**Example - Volume Control:**
```json
{
  "zone_id": "roon:15f84e4e2bfc07ea",
  "action": "vol_abs",
  "value": 45.0
}
```

**Response (200 OK):**
```json
{
  "status": "ok"
}
```

**Error Response (400):**
```json
{
  "error": "zone_id and action required"
}
```

**Error Response (500):**
```json
{
  "error": "control failed"
}
```

**Notes:**
- All control commands are fire-and-forget
- Clients should poll `/now_playing` to verify state changes
- Volume scale depends on backend: typically 0-100, but Roon uses dB (-80 to 0)
- The bridge handles rate-limiting internally (no client throttling needed)
- Some backends may not support all actions (e.g., UPnP lacks `next`/`prev`)

---

### 5. Device Configuration

Fetch per-device configuration settings.

```http
GET /config/<knob_id>
```

**Path Parameters:**
- `knob_id` (string, required): Device identifier (typically MAC address)

**Headers (optional):**
- `X-Knob-Version`: Firmware version string (for telemetry)

**Response (200 OK):**
```json
{
  "config_sha": "x7y9z2a5",
  "config": {
    "knob_id": "a1b2c3d4e5f6",
    "name": "Living Room Knob",
    "rotation_charging": 0,
    "rotation_not_charging": 180,
    "art_mode_charging": {
      "enabled": true,
      "timeout_sec": 60
    },
    "art_mode_battery": {
      "enabled": false,
      "timeout_sec": 0
    },
    "dim_charging": {
      "enabled": true,
      "timeout_sec": 120
    },
    "dim_battery": {
      "enabled": true,
      "timeout_sec": 30
    },
    "sleep_charging": {
      "enabled": true,
      "timeout_sec": 600
    },
    "sleep_battery": {
      "enabled": true,
      "timeout_sec": 180
    },
    "wifi_power_save_enabled": true,
    "cpu_freq_scaling_enabled": true,
    "sleep_poll_stopped_sec": 60
  }
}
```

**Config Fields:**
- `config_sha` (string): 8-char hash of config for change detection
- `config.knob_id` (string): Device identifier (echoed back)
- `config.name` (string): Human-readable device name
- `config.rotation_charging` (int): Display rotation in degrees (0/90/180/270) when charging
- `config.rotation_not_charging` (int): Display rotation when on battery
- `config.art_mode_*` (object): Full-screen artwork mode settings (enabled + timeout_sec)
- `config.dim_*` (object): Display dimming settings (enabled + timeout_sec)
- `config.sleep_*` (object): Display sleep settings (enabled + timeout_sec)
- `config.wifi_power_save_enabled` (bool): Enable WiFi power saving mode
- `config.cpu_freq_scaling_enabled` (bool): Enable CPU frequency scaling
- `config.sleep_poll_stopped_sec` (int): Polling interval when sleeping and playback stopped

**Error Response (400):**
```json
{
  "error": "knob_id required"
}
```

**Notes:**
- Clients should fetch config on startup and when `config_sha` changes in `/now_playing`
- Configuration is managed via the bridge's web interface (not documented here)
- Timeout values are in seconds; `0` typically means "disabled"
- The bridge automatically creates a config entry for new device IDs with defaults

---

## Client Identification

Clients can identify themselves via multiple methods (in order of precedence):

1. **HTTP Headers:**
   - `X-Knob-ID` or `X-Device-ID`: Device identifier
   - `X-Knob-Version` or `X-Device-Version`: Firmware version

2. **Query Parameters:**
   - `knob_id` in URL query string

3. **JSON Body:**
   - `knob_id` field in POST request body

The bridge uses this for:
- Per-device configuration management
- Telemetry and diagnostics
- Battery status tracking (future features)

---

## Multi-Backend Architecture

The bridge aggregates zones from multiple music sources:

- **Roon** (`roon:` prefix): Full metadata, zone grouping, Roon-specific features
- **Lyrion Music Server** (`lms:` prefix): LMS/Squeezebox player control
- **OpenHome** (`openhome:` prefix): OpenHome-compatible renderers
- **UPnP** (`upnp:` prefix): Basic DLNA MediaRenderer support (limited features)

Clients treat all zones identically—the prefix is opaque. The bridge routes control commands to the appropriate backend based on the zone_id prefix.

---

## Stability Notes

**This protocol is under active development.** Breaking changes may occur as we:
- Add support for new music sources
- Optimize for additional client hardware
- Implement new features based on community feedback
- Migrate to WebSocket for push updates (eliminating polling)

**What's likely to change:**
- New fields in existing responses (backward-compatible additions)
- New endpoints for advanced features (playlist management, search, etc.)
- Authentication/authorization mechanisms for non-local networks
- WebSocket support for real-time updates
- Image endpoint may add format options (WebP, AVIF)

**What's less likely to change:**
- Core endpoint paths (`/zones`, `/now_playing`, `/control`, `/config`)
- Basic JSON structure for existing fields
- Control action names (`play_pause`, `vol_abs`, etc.)
- Zone ID prefixing scheme

**Recommendations for implementers:**
- Gracefully ignore unknown JSON fields (forward compatibility)
- Handle HTTP 400/404/500 errors for missing zones/invalid actions
- Don't assume field presence—check for null/undefined
- Cache images by `image_key` to reduce bandwidth
- Poll `/now_playing` at reasonable intervals (respect the network)
- Follow semantic versioning if you build client libraries

---

## Example Client Flow

```
1. Discover bridge via mDNS (_roonextknob._tcp) or use configured URL
2. GET /config/<device-id> → Apply device-specific settings
3. GET /zones → Present zone picker to user
4. User selects zone → Save zone_id
5. Poll GET /now_playing?zone_id=<selected>&knob_id=<id>
6. Display line1, line2, volume, is_playing
7. Fetch GET /now_playing/image?zone_id=<selected> (cache by image_key)
8. On user input → POST /control with action
9. Watch for config_sha changes → Re-fetch config if changed
10. Watch for zones_sha changes → Re-fetch zones if changed
11. Repeat from step 5
```

---

## Reference Implementation

See [common/roon_client.c](https://github.com/muness/roon-knob/blob/master/common/roon_client.c) in the Roon Knob firmware for a complete ESP32-S3 client implementation in C.

## Questions or Feedback?

- **Roon Knob Issues:** https://github.com/muness/roon-knob/issues
- **Bridge Issues:** https://github.com/cloud-atlas-ai/unified-hifi-control/issues
- **Roon Community Discussion:** https://community.roonlabs.com/t/50-diy-roon-desk-controller/311363

---

**Last Updated:** 2026-01-07
**Protocol Version:** Draft (unreleased)
**Bridge Version:** 2.x (unified-hifi-control)
