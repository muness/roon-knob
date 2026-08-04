# ESP32-S3 Memory Architecture

This guide is the allocation policy for the ESP32-S3 targets. It explains why
"8 MB of PSRAM free" does not imply that a task, display buffer, or Bluetooth
controller can start, and how Dial divides memory between LVGL, Wi-Fi, BLE, and
adaptive UI payloads.

## The two useful memory classes

| Memory | Strengths | Constraints | Typical owners |
| --- | --- | --- | --- |
| Internal SRAM | Low latency, DMA-capable regions, available to cache-sensitive system paths | Small and easily fragmented; many ESP-IDF components require it | Display DMA buffers, radio and flash/NVS task stacks, UI task stack, control state |
| External PSRAM | Large capacity for data that can tolerate external-memory latency | Not DMA-capable for the Dial SPI path; unavailable while the external-memory cache is disabled | HTTP bodies, JSON trees, artwork pixels, inactive models, bridge worker stack, part of the LVGL object heap |

The Dial has 8 MB of octal PSRAM, but internal SRAM is usually the limiting
resource. Always distinguish these three measurements:

- total free internal heap;
- largest free internal block; and
- free/largest PSRAM blocks.

A 16 KiB stack needs one contiguous block. A controller may likewise fail its
private allocations even when total internal free memory looks adequate.

## Allocation policy

Use the required capability, not allocation size alone, to choose memory.

### Must remain internal

- SPI/QSPI DMA draw buffers (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`);
- the `ui_loop` stack;
- NimBLE host/service and other radio-controller task stacks;
- tasks that may execute while flash, NVS, or OTA disables the external-memory
  cache; and
- small latency-sensitive ownership, synchronization, and control objects.

### Prefer explicit PSRAM allocation

- bounded HTTP response bodies;
- JSON parse trees and serialized adaptive-screen payloads;
- bridge-to-UI media, connectivity, and configuration snapshots;
- decoded artwork and image workspaces that are not DMA sources;
- generated configuration, zone-picker, and BLE-management HTML;
- inactive-screen models and bounded caches; and
- background task stacks only when the task cannot touch flash/NVS and cannot
  run during cache-disabled operations.

Both targets set `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`. Consequently,
ordinary `malloc()` calls smaller than 16 KiB prefer internal RAM. That default
is useful for system code, but it makes many small JSON nodes and message
objects a fragmentation risk. Code that owns PSRAM-safe data should request
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` explicitly instead of lowering the global
threshold and changing allocation behavior for Wi-Fi, TLS, drivers, and other
components.

Dial additionally reserves 48 KiB of internal heap for allocations that
explicitly require internal or DMA-capable memory and places ESP-IDF's eligible
Wi-Fi/lwIP static BSS in PSRAM. Its two LVGL draw stripes remain DMA-capable and
double-buffered, but use 24 rows each (34,560 bytes total) instead of 36 rows
each (51,840 bytes). This returns 17,280 bytes to the shared display/radio DMA
budget. Frame has no LVGL draw buffers and retains the default 32 KiB internal
reserve.

ESP-IDF allocations made with `heap_caps_malloc()` can be released with
`free()`. Do not add an internal-RAM fallback for a large optional cache merely
to make allocation succeed: failing closed or reducing the cache is safer than
exhausting controller/DMA headroom.

## Dial's LVGL split pool

LVGL's built-in allocator normally reserves one fixed array in internal
`.bss`. At 64 KiB, that reservation remained present whether the current screen
used the bytes or not. It was therefore invisible to ordinary heap tuning and
competed directly with the BLE controller.

Dial now uses two LVGL TLSF pools:

| Pool | Size | Creation | Purpose |
| --- | ---: | --- | --- |
| Built-in internal pool | 24 KiB | `CONFIG_LV_MEM_SIZE_KILOBYTES=24` | Fast base capacity and an internal fallback for LVGL objects |
| PSRAM expansion pool | 72 KiB | `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=72`, then 16-byte-aligned `heap_caps_aligned_alloc()` and `lv_mem_add_pool()` | Additional widget/object capacity without a fixed internal reservation |

The order is important:

1. Initialize display hardware, but do not create LVGL objects.
2. Call `lv_init()` to create the internal pool.
3. Allocate an explicitly aligned PSRAM block and register it with
   `lv_mem_add_pool()`.
4. Register the display driver, fonts, and application UI.

LVGL's built-in TLSF compiles its maximum accepted pool size from
`LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE`. Reducing the internal base to 24 KiB
without setting a 72 KiB expansion budget makes the otherwise valid 72 KiB
PSRAM pool unconditionally fail registration. Alignment is still made explicit,
but it was not the cause of the observed rejection.

If the expansion pool cannot be allocated or registered, startup stops with an
explicit error. Continuing would create a device whose UI capacity depends on
accidental heap state.

Pool diagnostics log the backing address and size before registration. A boot
that does not reach `Added 73728-byte LVGL PSRAM expansion pool` has not tested
the split-pool design or BLE coexistence; it has stopped during LVGL setup.

The split changes the LVGL object budget from 64 KiB internal to 96 KiB total
while reclaiming 40 KiB of fixed internal SRAM. It does **not** move the
display's DMA draw buffers or the UI task stack to PSRAM. LVGL may satisfy an
individual object allocation from either registered pool, so feature sizing
must use `lv_mem_monitor()` rather than assuming all widgets are external.

## Payload lifecycle for adaptive UI

Capacity is not permission to retain every representation. For a downloaded
screen:

1. Fetch into a PSRAM buffer with a schema-specific hard limit.
2. Parse into PSRAM-backed nodes or an explicit PSRAM arena.
3. Validate before materializing LVGL objects.
4. Keep only the active screen materialized.
5. Release the serialized body and parser tree as soon as the retained model no
   longer needs them.
6. Bound and evict inactive models and decoded assets.

The generic JSON ceiling is 256 KiB through
`platform_http_get_bounded()`. Callers should select a smaller limit whenever
their schema permits. The HTTP implementations allocate response buffers in
PSRAM, accept known-length and chunked/unknown-length responses, grow only to
the hard ceiling, and always append a terminator.

The bridge installs process-global cJSON hooks once before its worker starts.
Those hooks keep cJSON's many small nodes in PSRAM and are never switched while
trees may be alive. Bridge-to-UI copied strings and view/configuration snapshots
use the same explicit external allocator.

## Task-stack rule

External stacks are an exception, not a general optimization. The bridge
network worker uses a 16 KiB PSRAM stack through `xTaskCreateWithCaps()` because
its former NVS-writing endpoint commit was moved to the internal UI task. The
TCB stays internal.

Before moving another stack, prove all of the following:

- the task does not write flash, NVS, or OTA state;
- it cannot run while the external-memory cache is disabled;
- it does not own a DMA/radio-controller path; and
- its failure mode is bounded and observable.

Pinning a task to a core controls scheduling contention; it does not change
which memory capabilities its stack requires.

## Telemetry and acceptance

Dial logs internal, DMA, and PSRAM free/largest blocks after the major boot
stages and immediately before/after BLE initialization. The UI task also logs:

- stack peak usage and remaining high-water mark;
- LVGL total, free, and largest-free bytes; and
- LVGL utilization and fragmentation.

Early samples are frequent enough to attribute BLE startup allocations; normal
operation reduces them to once per minute. Test real artwork, configuration,
adaptive navigation, Wi-Fi recovery, BLE pairing/reconnect, and OTA—not only an
idle boot.

Three failures established the current policy:

1. A 32 KiB UI stack could not be allocated after display setup because the
   largest internal block was 31,744 bytes, leaving a static initialized panel.
2. Moving the bridge worker and HTTP/HTML buffers to PSRAM improved the
   pre-NimBLE checkpoint from 4,867 bytes free / 1,600 largest to 21,055 free /
   10,240 largest, but the BLE controller still failed. Map analysis then found
   LVGL's fixed 64 KiB internal pool, leading to the split-pool design.
3. The first two split-pool artifacts returned from `app_main()` before UI or
   BLE startup because `LV_MEM_POOL_EXPAND_SIZE` remained zero. The second used
   a verified 16-byte-aligned PSRAM address and still failed, proving alignment
   was not the cause. LVGL's compiled TLSF pool-size limit must be configured
   alongside the runtime `lv_mem_add_pool()` call.

A green compile proves configuration and linkage, not runtime coexistence.
Hardware acceptance requires a live UI, successful BLE controller startup, and
stable memory/stack telemetry under the feature's real workload.

## Checklist for new memory-heavy features

- State a hard byte limit for every remote or decompressed payload.
- Choose memory with explicit capability flags.
- Record ownership and the exact point at which each large allocation is freed.
- Avoid simultaneous HTTP body, parse tree, decoded asset, inactive model, and
  multiple LVGL screen trees.
- Check both total and largest internal blocks before creating tasks/controllers.
- Check task high-water marks under the largest realistic call path.
- Check LVGL utilization and fragmentation across repeated screen transitions.
- Preserve internal headroom for Wi-Fi/BLE startup and reconnection, not just
  initial boot.
- Verify Dial and Frame separately; shared code does not imply identical memory
  geometry.

## Related documentation

- [Display subsystem](DISPLAY.md)
- [BLE HID host](BLE_HID.md)
- [Font strategy](FONTS.md)
- [Dial board overview](hw-reference/board.md)
- [Artwork rendering](hw-reference/image_render.md)
