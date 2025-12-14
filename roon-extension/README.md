# Roon Knob Bridge

Node.js sidecar that pairs with the local Roon Core, exposes a tiny HTTP API for the ESP32 + simulator clients, and advertises `_roonknob._tcp` via mDNS.

## Features

- Uses `node-roon-api` transport service to follow zones/now-playing data in real-time.
- HTTP adapter that the firmware/simulator can poll: `/zones`, `/now_playing`, `/control`, `/status`.
- Volume changes are rate-limited (<=10 updates/sec per zone) and coalesced to avoid Roon throttling.
- Publishes `base` + `api` TXT records so devices can discover the bridge automatically.

## Getting Started

```bash
cd roon-extension
npm install
PORT=8088 LOG_LEVEL=debug npm start
```

The process will auto-discover your Roon Core on the LAN. Keep the Core’s “Enable Extensions” screen open the first time to allow pairing.

## Environment Variables

| Variable | Description |
| --- | --- |
| `PORT` | **HTTP API listen port** (default `8088`). This is what the knob connects to. Set to `80` if you want to use the standard HTTP port. |
| `LOG_LEVEL` | `debug` for verbose HTTP logs, otherwise `info` (default `info`). |
| `MDNS_NAME` | Friendly service name advertised over `_roonknob._tcp`. |
| `ROON_SERVICE_PORT` | Internal Roon discovery protocol port (default `9330`). **Not the HTTP port.** Only change if you have port conflicts with another Roon extension. |
| `MDNS_BASE` | Optional base URL to advertise via mDNS (default `http://<hostname>:PORT`). |
| `CONFIG_DIR` | Directory for persistent config storage (default `./data`). Stores Roon pairing tokens after authorization. |

### Common Port Confusion

- **`PORT`** = HTTP API port (what the knob talks to). Change this to `80` if desired.
- **`ROON_SERVICE_PORT`** = Internal Roon discovery socket. Rarely needs changing.

Example to run on port 80:
```bash
PORT=80 npm start
```

### Config Persistence

The `CONFIG_DIR` stores `config.json` containing Roon pairing tokens. This file is **only created after you authorize the extension** in Roon:

1. Start the bridge
2. Open Roon → Settings → Extensions
3. Find "Roon Knob Bridge" and click Enable/Authorize
4. The pairing token is saved to `CONFIG_DIR/config.json`

Without this authorization step, the data directory will remain empty.

### mDNS Notes

By default the knob ignores discovered bridges whose hostname isn't a numeric IP (to avoid unresolvable names). If your mDNS advert uses a DNS name, set `MDNS_BASE` to the numeric IP that the knob can reach instead.

## Docker Setup

Example `docker-compose.yml`:

```yaml
services:
  roon-knob-bridge:
    build: ./roon-extension
    network_mode: host
    environment:
      PORT: "8088"           # HTTP API port (change to 80 if desired)
      CONFIG_DIR: /app/data  # Must match volume mount path
    volumes:
      - ./data:/app/data     # Persist Roon pairing tokens
    restart: unless-stopped
```

**Important**: The `CONFIG_DIR` environment variable must match the container-side path of your volume mount. If you mount `./data:/app/data`, set `CONFIG_DIR=/app/data`.

### Running Multiple Extensions

If you need to run another Roon extension on the same host, remap the host side of `ports:` while leaving the container side unchanged. The entries are `HOST:CONTAINER`, so `9002:8088` exposes the bridge on host port 9002.

When using bridge networking (not `network_mode: host`):

```yaml
ports:
  - "9002:8088"    # HTTP API
  - "9392:9330"    # Roon discovery (TCP)
  - "9392:9330/udp"  # Roon discovery (UDP)
environment:
  PORT: "8088"              # Keep internal port unchanged
  ROON_SERVICE_PORT: "9330" # Keep internal port unchanged
```

If the DNS name advertised by mDNS isn't reachable from the knob, set `MDNS_BASE` to `http://<ip>:<port>` so the discovery TXT record points at a resolvable address.

## HTTP Contract

- `GET /zones` → `[ { zone_id, zone_name } … ]`
- `GET /now_playing?zone_id=<id>` → `{ line1, line2, is_playing, volume, volume_step, image_url }`
- `GET /now_playing/image?zone_id=<id>` → Placeholder SVG until album-art proxy lands.
- `POST /control` → `{ zone_id, action, value? }` (`action ∈ { play_pause, vol_rel, vol_abs }`)
- `GET /status` → `{ status, version }`
- `GET /image?image_key=…` → 204 placeholder until album-art proxy lands.
- `GET /admin` – Human dashboard showing Roon pairing state, zones, recent knob clients, and mDNS info (polls `/admin/status.json`).

There is also `GET /now_playing/mock` for UI smoke tests without a running Core.

### Identifying Knobs

If the firmware or simulator includes `X-Knob-Id` and `X-Knob-Version` headers on requests, the dashboard will show each device’s last activity (zone, timestamp, IP). Fall back to anonymous IP tracking if those headers are missing.

## Notes

- On first launch, Roon will prompt you to enable "Roon Knob Bridge".
- If you change ports or hostnames, restart the process so mDNS re-advertises the new TXT `base`.
- `hqplayer_profile_switcher.js` remains as a reference add-on module from your other extension.
