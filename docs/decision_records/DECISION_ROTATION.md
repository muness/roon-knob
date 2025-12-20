# Decision: Only Support 0° and 180° Display Rotation

**Date:** 2025-12-20
**Status:** Accepted

## Context

The Roon Knob has a 360x360 round LCD that can be mounted in different orientations. When the device is on a charging stand, users may want to flip the display 180° so it faces the correct direction.

LVGL 9.x in PARTIAL render mode (using 36-row draw buffers for memory efficiency) does not support built-in rotation. Software rotation must be implemented in the flush callback.

## Decision

Only support 0° and 180° rotation. Do not support 90° or 270° rotation.

## Rationale

### 180° Rotation is Fast

180° rotation is a simple pixel reversal operation:
```c
for (int i = 0; i < pixel_count; i++) {
    dst[pixel_count - 1 - i] = src[i];
}
```

This has excellent cache locality - sequential reads and mostly sequential writes. Performance impact is negligible.

### 90°/270° Rotation is Slow

90° and 270° rotation require a matrix transpose operation where each source row maps to a destination column:

```c
// 90° clockwise: (x, y) -> (height-1-y, x)
for (int y = 0; y < src_h; y++) {
    for (int x = 0; x < src_w; x++) {
        dst[x * dst_w + (src_h - 1 - y)] = src[y * src_w + x];
    }
}
```

This has terrible cache locality:
- Each iteration of the inner loop jumps by `dst_width` bytes in memory
- For a 360×36 flush area, this means 12,960 scattered writes
- PSRAM (where the rotation buffer lives) is already ~4x slower than internal RAM
- The combination results in very noticeable UI lag

### Use Case Analysis

The primary use case for rotation is mounting the device on a charging stand where the "up" direction is reversed. This requires exactly 180° rotation.

90° and 270° rotation would only be useful for mounting the device sideways, which is not a realistic use case for a round knob controller.

## Consequences

- Users can flip the display for charging stands (the main use case)
- 90° and 270° options will be rejected with a warning log
- The bridge admin UI should only offer 0° and 180° rotation options
- If 90°/270° support is needed in the future, consider:
  - Block-based tiling to improve cache locality
  - ESP32-S3 PPA (Pixel Processing Accelerator) hardware
  - Accepting the performance cost for specific use cases

## Alternatives Considered

1. **Block tiling**: Process the image in small cache-friendly blocks. Would improve performance but adds significant complexity.

2. **Hardware acceleration**: ESP32-S3 has a PPA but it's complex to integrate with LVGL's flush pipeline.

3. **Pre-rotated assets**: Have the bridge send pre-rotated album art. Doesn't help because we rotate the entire rendered frame (text, controls, etc.), not just images.

4. **Accept the slowness**: Could ship 90°/270° with known performance issues. Rejected because the poor UX isn't worth supporting an unlikely use case.
