# RGB565 Image Format Implementation

**Status:** Implemented as of v2.3+
**Related:** Issue #87

## Overview

Firmware now receives artwork as raw RGB565 data from the bridge instead of JPEG. This eliminates local JPEG decoding overhead.

## Benefits

**Memory:**
- No JPEG decoder library (64KB firmware reduction)
- No decode overhead in PSRAM
- Same 311KB global buffer used for display

**CPU:**
- Zero decode cycles
- Direct copy to display buffer

**Trade-off:**
- Network transfer: ~260KB RGB565 vs ~30-50KB JPEG
- Acceptable on local Gigabit networks

## Implementation

### Bridge Request

**Endpoint:** `GET /now_playing/image?zone_id=X&width=360&height=360&scale=fit&format=rgb565`

**Response:**
- `Content-Type: application/octet-stream`
- `Content-Length: 259200` (360 × 360 × 2 bytes)
- Raw RGB565 data (little-endian)

### Firmware Flow

1. **Fetch** - HTTP GET returns 259,200 bytes
2. **Validate** - Check size matches expected (360×360×2)
3. **Copy** - memcpy to global PSRAM buffer (ui_jpeg.c:191)
4. **Display** - LVGL descriptor points to buffer
5. **Byte swap** - Display flush callback converts to big-endian for SH8601 QSPI (platform_display_idf.c:329-333)

### Code Locations

- **URL construction:** `common/bridge_client.c:1091`
- **RGB565 handler:** `common/ui_jpeg.c:165-205` (`ui_rgb565_from_buffer`)
- **Size validation:** `common/ui.c:1241-1247`
- **Byte swap:** `esp_dial/main/platform_display_idf.c:329-333`

## Byte Order

**Bridge sends:** Little-endian RGB565
**Display expects:** Big-endian RGB565 (SH8601 QSPI)
**Handled by:** Flush callback swaps bytes before sending to display

## Error Handling

If bridge returns unexpected size (not 259,200 bytes):
- Log warning with expected vs actual size
- Hide artwork
- Continue operation (graceful degradation)

## Requirements

- Bridge must support `format=rgb565` parameter
- Bridge must return exactly width × height × 2 bytes
- Network must handle ~260KB transfers reliably

## Testing

**Monitor logs for:**
```
I (12345) UI: Processing raw RGB565 format (259200 bytes)
I (12346) UI_RGB565: Loaded raw RGB565 360x360 (259200 bytes) into global buffer
```

**Check for errors:**
```
W (12345) UI: Unexpected image size: XXXXX bytes (expected 259200 for 360x360 RGB565)
```

## Future Improvements

- Add ETag/If-None-Match caching
- Support variable image sizes
- Content-Type header checking
