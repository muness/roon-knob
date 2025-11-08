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
| `PORT` | HTTP listen port (default `8088`). |
| `LOG_LEVEL` | `debug` for verbose HTTP logs, otherwise `info`. |
| `MDNS_NAME` | Friendly service name advertised over `_roonknob._tcp`. |
| `ROON_SERVICE_PORT` | TCP port used for the Roon discovery socket (defaults to `9330`). |

## HTTP Contract

- `GET /zones` → `[ { zone_id, zone_name } … ]`
- `GET /now_playing?zone_id=<id>` → `{ line1, line2, is_playing, volume, volume_step }`
- `POST /control` → `{ zone_id, action, value? }` (`action ∈ { play_pause, vol_rel, vol_abs }`)
- `GET /status` → `{ status, version }`
- `GET /image?image_key=…` → 204 placeholder until album-art proxy lands.
- `GET /admin` – Human dashboard showing Roon pairing state, zones, recent knob clients, and mDNS info (polls `/admin/status.json`).

There is also `GET /now_playing/mock` for UI smoke tests without a running Core.

### Identifying Knobs

If the firmware or simulator includes `X-Knob-Id` and `X-Knob-Version` headers on requests, the dashboard will show each device’s last activity (zone, timestamp, IP). Fall back to anonymous IP tracking if those headers are missing.

## Notes

- On first launch, Roon will prompt you to enable “Roon Knob Bridge”.
- If you change ports or hostnames, restart the process so mDNS re-advertises the new TXT `base`.
- `hqplayer_profile_switcher.js` remains as a reference add-on module from your other extension.
