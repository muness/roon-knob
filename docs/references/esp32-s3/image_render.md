Distributing Album Art to ESP32‑S3 – Practical Guide

Goal
- Deliver now‑playing artwork from the sidecar to the ESP32‑S3 reliably and cheaply in RAM/CPU, then render it with LVGL on a 360×360 panel.

Constraints on ESP32‑S3
- RAM: app has 16 MB PSRAM, but avoid large contiguous allocations. Our current HTTP helper allocates a single buffer sized to `Content-Length`.
- CPU: JPEG decode is OK if using a lightweight decoder; PNG is heavier on CPU/RAM.
- Networking: stable when requests are short‑lived and responses are < ~64 KB.

Transport Recommendations (HTTP)
- Endpoint: `GET /now_playing/image?zone_id=…&width=…&height=…&scale=fit&format=jpeg`
- Always return `Content-Length` (not chunked) so the client can pre‑allocate the buffer.
- Set `Content-Type: image/jpeg` (baseline), and target ≤ ~30–50 KB per image at thumbnail sizes (e.g., 128–256 px square).
- Add caching: `ETag: <image_key>-<w>x<h>-<scale>` and `Cache-Control: public, max-age=60`.
- Support `If-None-Match` on the server to return `304 Not Modified` when unchanged.

Sidecar (Server) Knobs
- Proxy via RoonApiImage with `width/height/scale` to bound bytes. Use `format=jpeg` (if available) and re‑encode if the core cannot.
- Optional: provide `format=rgb565` that returns a raw RGB565 frame (**big‑endian/byte-swapped** to match SH8601 QSPI display) with `Content-Type: application/octet-stream` for zero‑decode on the device. Include `X-Width`, `X-Height` headers.
- Return placeholder SVG/PNG only when artwork is absent.

Decoder choices (ESP32‑S3)
- TJpgDec (Tiny JPEG Decompressor)
  - Small footprint; proven on ESP; LVGL adapter available (lv_lib_tjpgd).
  - Feed from the HTTP buffer via read callbacks; write RGB565 scanlines into an LVGL image/canvas.
- ESP‑IDF esp_jpeg (IDF 5.x) **[Currently Used]**
  - Native component with examples; decode to RGB565 buffer (PSRAM‑backed) and blit.
  - **Important**: Use `JPEG_PIXEL_FORMAT_RGB565_BE` (big-endian) output to match the SH8601 QSPI display's byte order. See `common/ui_jpeg.c`.
- Others (Bitbank JPEGDEC / TJpg_Decoder)
  - Viable; prefer TJpgDec/esp_jpeg for clean LVGL/IDF integration.

Client (ESP32‑S3) Options
1) Decode JPEG on device (recommended flexible path)
   - Use a lightweight JPEG decoder:
     - ESP‑IDF: `esp_jpeg` component (IDF examples exist), or TinyJPG/TJpgDec port.
     - LVGL: register a JPEG decoder (lvgl 9: external decoder modules).
   - Flow:
     - HTTP GET image → buffer (cap size to, say, 64 KB). Our `platform_http_idf` allocates exactly `Content-Length`.
     - Decode to RGB565 lines; blit into an LVGL image or canvas buffer sized to the chosen thumbnail (e.g., 180×180).
     - Free the network buffer promptly.

2) Stream RGB565 (server pre‑convert)
   - Pro: no decode on device; just copy to LVGL.
   - Con: bigger payload than JPEG; requires server conversion; endianness/pixel order must match panel expectations.

Sizing & Layout on 360×360
- Suggested thumbnail sizes: 128×128 (fast), 180×180 (balanced), up to 240×240 if PSRAM allows.
- Place art under the zone label; push line1/line2 below the image (see UI layout task).

HTTP client notes (our implementation)
- `platform_http_idf` calls `esp_http_client_fetch_headers()` then allocates a buffer equal to `Content-Length` and reads once. Implications:
  - Server MUST provide `Content-Length`. Avoid chunked responses.
  - Keep images small; we free the buffer after decode.
- Timeouts: keep request timeout ~3 s; reduce on idle refreshes.

Caching on device
- Keep the last ETag per zone and send `If-None-Match` (to be added to `platform_http_idf` helper) to avoid re‑downloading unchanged art.
- LRU cache of the last 1–2 decoded thumbnails (optional; store in PSRAM).

Security/Robustness
- Whitelist content types (`image/jpeg`, `application/octet-stream` for rgb565); ignore others.
- Reject images > a safe cap (e.g., 128 KB) to avoid RAM pressure.
- Handle decode errors gracefully and fall back to placeholder.

Example Sequence
1) Text: `GET /now_playing?zone_id=Z` → line1/line2/is_playing/volume/image_key
2) Art: `GET /now_playing/image?zone_id=Z&width=180&height=180&scale=fit` → JPEG ≤50 KB
3) Render: decode → LVGL `img` or canvas and place under zone name.

References
- RoonApiImage: `get_image(image_key, {scale,width,height,format}, cb)` (proxied by sidecar)
- LVGL image decoder docs; ESP‑IDF `esp_jpeg` example
- **Color Configuration**: See `docs/references/COLORTEST_HELLOWORLD.md` for SH8601 display byte order and LVGL 9 color format setup

