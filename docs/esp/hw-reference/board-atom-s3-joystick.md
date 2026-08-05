# HiPhi Joy (M5Stack AtomS3 Joystick)

HiPhi Joy is a distinct M5 surface, not a small Tough. It uses the AtomS3's
128×128 display through M5Unified/M5GFX and reads the joystick base's STM32
coprocessor through the documented I²C interface.

This page records the qualified hardware facts and the implementation decisions
that matter when changing the Atom target. The firmware target is
`atom_app/`; the shared controller and bridge contracts remain target-neutral.

## Hardware contract

- Host: M5Stack AtomS3 (`m5::board_t::board_M5AtomS3`), ESP32-S3.
- Display: 128×128 color LCD, owned by M5GFX. The target is initialized as
  `m5::board_t::board_M5AtomS3`, rotation 0, with M5GFX byte swapping enabled
  for artwork data.
- Flash/build profile: ESP32-S3 with 8 MB flash, performance optimization, and
  **no PSRAM** (`CONFIG_SPIRAM=n`). Do not infer a larger memory profile from
  another AtomS3 product revision.
- Joystick coprocessor: address `0x59`, SDA GPIO38, SCL GPIO39, 400 kHz.
- Registers used by this target: left-stick 8-bit values at `0x10`, right-stick
  12-bit values at `0x20`/`0x22`, and button values at `0x70`. The 12-bit right
  stick reads are normalized at the platform boundary to the UI's 8-bit axis
  contract because the separate right-stick 8-bit summary registers were not
  updating reliably on the qualified firmware revision.
- Four button bits are exposed by `M5Platform` as left, right, A, and B.

The controller UI is glance-first: album art and now-playing text share the
small display, while the sticks and four buttons provide transport, volume,
and zone-picker input. There is no touchscreen assumption.

## Display and memory contract

`M5GFX::pushImage()` is a raw pixel-buffer primitive. It does not fetch, decode,
or own image memory. The Atom implementation therefore keeps image transport
and display rendering separate:

1. The shared HTTP layer streams the response in chunks.
2. Atom UI owns one small RGB565 tile buffer (`128 × 4` pixels, about 1 KiB).
3. Each completed tile is pushed at its native destination with `pushImage()`.

Control mode requests a native 72×72 thumbnail at `(4,18)`. Artwork mode
requests a native 128×128 frame at `(0,0)` and intentionally shows nothing but
artwork. There is no scaling, stretching, full-frame canvas, JPEG decode buffer,
or repeated 32 KiB allocation on this target.

Text and transient feedback use persistent region sprites rather than painting
clear/fill/text phases directly onto the panel:

- top zone/status strip: 128×18 RGB565;
- metadata, marquee, seek, and acknowledgement band: 128×38 RGB565;
- both regions are filled and composed offscreen, then pushed once.

Together these sprites use about 14 KiB of RGB565 storage. This is a deliberate
region-buffer compromise: it removes visible repaint flashes without requiring
the PSRAM-backed full-screen canvases used by some other M5GFX examples.

The M5GFX one-argument `setTextColor(foreground)` form is transparent. The UI
uses it while drawing into a filled region sprite; opaque text should use the
foreground/background overload only when that behavior is intended. Clipping
for marquees is bounded to the row and restored after each draw.

## Input map

The platform reads the joystick coprocessor at 400 kHz and presents normalized
8-bit axes to the UI. In the control page:

| Control | Action |
| --- | --- |
| Left stick up/down | Volume up/down; held movement repeats with an initial delay and faster steady cadence |
| Right stick left/right | Previous/next track |
| R / top-right button short press | Open the zone selector; short press again while selecting closes it |
| Atom surface button | Play/pause |
| Left-stick press | Play/pause |
| Top-left button | Toggle artwork-only mode |
| Right-stick press held | Open the zone selector |

While the zone selector is open, vertical or horizontal movement from either
stick is accepted. The right stick takes precedence when both report movement;
the left stick remains a fallback for hardware/firmware-axis variation. A
selection is not committed by movement alone; the existing shared action router
performs the zone selection.

The physical button register is active-low. The current qualified mapping is
`0x70`: top-left, top-right, left-stick press, right-stick press. The right
stick's reliable values are the 12-bit little-endian registers `0x20` and
`0x22`; the separate 8-bit summary registers were not reliable on the tested
coprocessor firmware revision, so the platform normalizes the 12-bit readings
to the UI's 8-bit axis contract.

## Now-playing state and layout

The control page retains the 72×72 thumbnail, a play/pause state glyph, and
three metadata rows for artist/title/album. Long rows scroll independently,
including when artwork is unavailable. The seek indicator is below the album
row and advances locally from the latest position/duration update while
playing; unknown or non-positive duration safely disables the filled portion.

Transient action acknowledgements (including volume current/max feedback) are
composed into the metadata region. They do not trigger a full-screen clear, and
artwork-only mode suppresses all text and acknowledgement overlays.

## Build

```sh
source "$HOME/esp/esp-idf/export.sh"
idf.py -C atom_app set-target esp32s3
idf.py -C atom_app build
# With the qualified device connected:
idf.py -C atom_app -p /dev/cu.usbmodem2101 flash
```

The target uses the shared NVS/configuration and Wi-Fi provisioning contract.
Do not erase NVS during ordinary firmware updates.

## Validation status

The target has been compiled with ESP-IDF 5.5.5 and flashed to the qualified
AtomS3 device at `/dev/cu.usbmodem2101`. Serial boot and the network/artwork
startup path have been exercised. Visual confirmation of sustained marquee,
seek movement, and absence of flicker remains a physical-display check after
UI changes; a green build alone is not hardware validation.
