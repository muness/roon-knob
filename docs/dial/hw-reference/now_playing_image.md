Now Playing Graphic – App and Bridge Notes

Purpose
- Surface the album art/now‑playing graphic so firmware, simulator, and future web dashboards can show music artwork alongside the textual metadata.

Current behavior
- `/now_playing` returns textual summary (line1, line2, is_playing, volume, etc.) but no artwork.
- The bridge advertises `/image` as a 204 placeholder (per README); devices simply fall back to generic UI states.

Proposed flow
1. Firmware / simulator request `/now_playing?zone_id=…` as before for text data.
2. When they need art, they subsequently request `GET /now_playing/image?zone_id=…`.
3. The bridge proxies the request to Roon’s transport library (via `state.nowPlayingByZone`) to determine if the core has `image_key`/`image_data` for that zone.
4. If artwork is available, stream it back with the correct content type.
5. If not, return a lightweight placeholder SVG (currently under `/assets/now_playing.svg`) so the UI can render a consistent motif while we wait for a real art proxy.

Bridge implementation notes
- New endpoint: `GET /now_playing/image` takes `zone_id` query param (required).
- It checks `bridge.getNowPlaying(zone_id)` for cached data. When image metadata exists, build an HTTP fetch to the Roon image endpoint (`transport.request("image", { zone_id, image_key })`) or a future proxy.
- When no image is known, respond with a static SVG fallback (provided under `public/assets/now_playing.svg`).
- Include proper caching headers (ETag/Cache-Control) once real artwork is available.

Firmware/Simulator expectations
- Request the image after the textual response (they share the same zone_id).
- Fallback path: if `/now_playing/image` responds 204/404, continue showing text + placeholder.
- Later the UI can detect the placeholder’s SVG fill for consistent layout.

Future tasks
- Track discovery/proxy of Roon’s remote artwork so we can stream real JPEG/PNG data.
- Expose new API to let the bridge revalidate image_key when zone changes.

