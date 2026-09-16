#!/usr/bin/env python3
"""Generate the HiPhi boot-logo C assets in common/assets/ from the master PNG.

The generated sources are committed. This script exists so they can be
regenerated when the brand mark changes; it is not run by the build.

Requirements
------------
Pillow. Install it into the same Python used elsewhere in this repo:

    export PATH="$HOME/.pyenv/versions/3.12.9/bin:$PATH"
    pip install pillow

Usage
-----
    python3 scripts/generate_boot_logo.py            # regenerate common/assets/*
    python3 scripts/generate_boot_logo.py --preview  # also write docs/images/previews/

`--preview` decodes the generated C arrays back into PNGs so the committed
bytes - not the intermediate Pillow images - are what you look at.

Source of truth
---------------
docs/images/hiphi-logo-512.png, copied from
open-horizon-labs/unified-hifi-control @ v4 `assets/icons/icon-512.png`.
There is no vector wordmark in either the hiphi site repo or the UHC repo
(checked 2026-09-16), so the 512 px raster is the largest master available and
every size here is a LANCZOS downscale from it rather than from the 64 px
`public/hifi-logo.png`, which would alias badly.

Output formats
--------------
common/assets/hiphi_logo_lvgl.c   LVGL 9 `lv_image_dsc_t`:
    * hiphi_logo_64_argb8888 / hiphi_logo_48_argb8888 - ARGB8888. The Dial
      draws the 48 px one; see common/ui.c for why 48 and not 64. Native sizes
      rather than lv_image_set_scale(), which keeps the source-sized bounding
      box and would clip. ARGB8888 rather than RGB565A8 so no assumption is
      made about the Dial's LVGL RGB565 byte order; the extra 4 KB is noise on
      a 16 MB part.
    * hiphi_logo_mono_64_a8   - A8 (alpha-only), 64 px, for Slate. Its alpha is
      the 1-bit ink mask, so LVGL recolors it to solid black and the reflective
      panel's own Bayer threshold passes it through unchanged instead of
      re-dithering a photographic gradient.

common/assets/hiphi_logo_rgb565.c  M5GFX/LovyanGFX targets (Tough, Joy, M5 beta):
    uint16_t RGB565 arrays plus 1-bit alpha bitmaps at 64, 48 and 32 px.
    RGB565 words are host order: (R>>3)<<11 | (G>>2)<<5 | (B>>3).
    IMPORTANT byte order: LovyanGFX's `get_depth<uint16_t>` resolves to
    `rgb565_2Byte`, which is the BYTE-SWAPPED layout. `lgfx::rgb565_t` is
    `rgb565_nonswapped`, i.e. host order. The inline helper in the header
    therefore casts to `const lgfx::rgb565_t*` rather than passing a raw
    `uint16_t*`, which would swap red and blue.

common/assets/hiphi_logo_mono.c    1-bit packed, MSB first, row padded to a
    byte boundary; 1 = ink. 64 px for the Frame's ACeP e-ink panel (drawn as
    EINK_BLACK on EINK_WHITE).

    On the 1-bit reduction: plain Floyd-Steinberg error diffusion of this mark
    is illegible at 64 px - see mono_ink_mask() for why, and `--mono-mode
    dither --preview` to see it. The default is a high-pass ink separation
    that keeps the disc solid and the waveform readable.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageFilter
except ImportError:  # pragma: no cover - operator-facing message
    sys.exit(
        "Pillow is required. See the header of this file:\n"
        '  export PATH="$HOME/.pyenv/versions/3.12.9/bin:$PATH" && pip install pillow'
    )

ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "docs" / "images" / "hiphi-logo-512.png"
FALLBACK_MASTER = ROOT / "docs" / "images" / "hiphi-logo-64.png"
OUT_DIR = ROOT / "common" / "assets"
PREVIEW_DIR = ROOT / "docs" / "images" / "previews"

# Sizes emitted for the M5GFX/LovyanGFX targets.
#   64 - Tough (320x240)
#   48 - M5 beta family: Dial Lab, Twist, Remote
#   32 - Joy (AtomS3, 128x128); 48 leaves no room beside the SSID + scan list
RGB565_SIZES = (64, 48, 32)
# LVGL ARGB8888 descriptors: 64 px is the reference size, 48 px is what the
# Dial's setup screen can actually fit (see common/ui.c).
LVGL_ARGB_SIZES = (64, 48)
LVGL_SIZE = 64
MONO_SIZE = 64

BANNER = """\
/*
 * GENERATED FILE - DO NOT EDIT BY HAND.
 *
 * Regenerate with:  python3 scripts/generate_boot_logo.py
 * Source master:    docs/images/hiphi-logo-512.png
 *
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
"""


# ---------------------------------------------------------------------------
# Image preparation
# ---------------------------------------------------------------------------


def load_master() -> Image.Image:
    path = MASTER if MASTER.exists() else FALLBACK_MASTER
    if not path.exists():
        sys.exit(f"No master logo found at {MASTER} or {FALLBACK_MASTER}")
    image = Image.open(path).convert("RGBA")
    # The UHC icon carries a wide transparent margin. Crop to the alpha bounding
    # box first so the requested size is the size of the mark, not of padding.
    bbox = image.getchannel("A").getbbox()
    if bbox:
        image = image.crop(bbox)
    # Pad back to a square so the circular mark is not distorted by the resize.
    side = max(image.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    return square


def scaled(master: Image.Image, size: int) -> Image.Image:
    return master.resize((size, size), Image.LANCZOS)


MONO_BLUR_RADIUS = 2.0
MONO_DETAIL_CUTOFF = 8


def mono_ink_mask(
    master: Image.Image, size: int, mode: str = "highpass"
) -> list[list[int]]:
    """Reduce the mark to 1 bit. Returns rows of 1 (ink) / 0 (paper).

    `mode="dither"` is the straightforward Floyd-Steinberg error diffusion of
    the mark composited onto white. It is kept for comparison but is NOT the
    default, because it is illegible at this size: the mark is a glossy
    mid-tone disc (the luminance histogram is dominated by a 64-128 band) whose
    only distinguishing features are thin light strokes. Error diffusion turns
    that into an even 40%-grey noise field - a blob, not a logo. Render it with
    `--mono-mode dither --preview` and compare if you want to see this.

    `mode="highpass"` (default) separates ink from detail instead of diffusing
    error. The disc becomes solid ink; the waveform and transport glyphs, which
    are thin strokes brighter than their local neighbourhood, stay as paper.
    The result is a crisp, recognisable mark at 64 px on a 1-bit panel.
    """
    rgba = scaled(master, size)
    white = Image.new("RGB", rgba.size, (255, 255, 255))
    white.paste(rgba, mask=rgba.getchannel("A"))
    luma = white.convert("L")

    if mode == "dither":
        # Pillow's "1" conversion uses Floyd-Steinberg error diffusion.
        pixels = luma.convert("1").load()
        return [[0 if pixels[x, y] else 1 for x in range(size)] for y in range(size)]

    blurred = luma.filter(ImageFilter.GaussianBlur(MONO_BLUR_RADIUS))
    detail = ImageChops.subtract(luma, blurred)
    detail_px = detail.load()
    alpha_px = rgba.getchannel("A").load()
    return [
        [
            1
            if alpha_px[x, y] >= 128 and detail_px[x, y] < MONO_DETAIL_CUTOFF
            else 0
            for x in range(size)
        ]
        for y in range(size)
    ]


def pack_bits(rows: list[list[int]]) -> tuple[bytes, int]:
    """Pack rows of 0/1 MSB-first, each row padded to a byte boundary."""
    size = len(rows[0])
    stride = (size + 7) // 8
    out = bytearray()
    for row in rows:
        line = bytearray(stride)
        for x, bit in enumerate(row):
            if bit:
                line[x >> 3] |= 0x80 >> (x & 7)
        out += line
    return bytes(out), stride


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# ---------------------------------------------------------------------------
# C emission helpers
# ---------------------------------------------------------------------------


def c_bytes(data: bytes, per_line: int = 12, indent: str = "    ") -> str:
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i : i + per_line]
        lines.append(indent + " ".join(f"0x{b:02x}," for b in chunk))
    return "\n".join(lines)


def c_words(words: list[int], per_line: int = 10, indent: str = "    ") -> str:
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i : i + per_line]
        lines.append(indent + " ".join(f"0x{w:04x}," for w in chunk))
    return "\n".join(lines)


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    print(f"wrote {path.relative_to(ROOT)} ({len(text.encode()):,} bytes)")


# ---------------------------------------------------------------------------
# Generators
# ---------------------------------------------------------------------------


def gen_lvgl(master: Image.Image, mono_mode: str) -> None:
    argb_blocks = []
    argb_externs = []
    for size in LVGL_ARGB_SIZES:
        rgba = scaled(master, size)
        # ARGB8888: LVGL stores these little-endian as B,G,R,A in memory.
        argb = bytearray()
        for y in range(size):
            for x in range(size):
                r, g, b, a = rgba.getpixel((x, y))
                argb += bytes((b, g, r, a))
        argb_blocks.append(
            f"""static const uint8_t hiphi_logo_{size}_argb8888_map[] = {{
{c_bytes(bytes(argb))}
}};

const lv_image_dsc_t hiphi_logo_{size}_argb8888 = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = {size},
        .h = {size},
        .stride = {size} * 4,
        .reserved_2 = 0,
    }},
    .data_size = sizeof(hiphi_logo_{size}_argb8888_map),
    .data = hiphi_logo_{size}_argb8888_map,
    .reserved = NULL,
    .reserved_2 = NULL,
}};

"""
        )
        argb_externs.append(
            f"""/* Full-colour {size} px mark, ARGB8888. */
extern const lv_image_dsc_t hiphi_logo_{size}_argb8888;

"""
        )

    size = LVGL_SIZE
    # A8 from the 1-bit ink mask, for Slate's reflective panel.
    ink = mono_ink_mask(master, size, mono_mode)
    a8 = bytearray()
    for y in range(size):
        for x in range(size):
            a8.append(0xFF if ink[y][x] else 0x00)

    header = f"""{BANNER}
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define HIPHI_LOGO_LVGL_SIZE {size}

/*
 * Native sizes rather than one asset plus lv_image_set_scale(): a scaled
 * lv_image keeps its source-sized bounding box, so the Dial's smaller mark
 * would be clipped. The Dial uses the 48 px descriptor (see common/ui.c for
 * why 48 and not 64); Slate uses the 64 px A8 mask below.
 */
{"".join(argb_externs)}/* {size} px 1-bit ink mask as an alpha-only image. LVGL recolors it with the
 * image's recolor style property, so set that to black. Used by Slate
 * (rlcd_app), whose panel is 1-bit: pure black/white survives the panel's own
 * Bayer threshold exactly instead of being re-dithered. */
extern const lv_image_dsc_t hiphi_logo_mono_{size}_a8;

#ifdef __cplusplus
}}
#endif
"""

    source = f"""{BANNER}
#include "assets/hiphi_logo_lvgl.h"

{"".join(argb_blocks)}static const uint8_t hiphi_logo_mono_{size}_a8_map[] = {{
{c_bytes(bytes(a8))}
}};

const lv_image_dsc_t hiphi_logo_mono_{size}_a8 = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_A8,
        .flags = 0,
        .w = {size},
        .h = {size},
        .stride = {size},
        .reserved_2 = 0,
    }},
    .data_size = sizeof(hiphi_logo_mono_{size}_a8_map),
    .data = hiphi_logo_mono_{size}_a8_map,
    .reserved = NULL,
    .reserved_2 = NULL,
}};
"""
    write(OUT_DIR / "hiphi_logo_lvgl.h", header)
    write(OUT_DIR / "hiphi_logo_lvgl.c", source)


def gen_rgb565(master: Image.Image) -> None:
    blocks = []
    externs = []
    for size in RGB565_SIZES:
        rgba = scaled(master, size)
        words = []
        alpha_rows = []
        for y in range(size):
            row = []
            for x in range(size):
                r, g, b, a = rgba.getpixel((x, y))
                words.append(rgb565(r, g, b))
                row.append(1 if a >= 128 else 0)
            alpha_rows.append(row)
        packed, stride = pack_bits(alpha_rows)
        blocks.append(
            f"""const uint16_t hiphi_logo_rgb565_{size}[{size} * {size}] = {{
{c_words(words)}
}};

const uint8_t hiphi_logo_alpha_{size}[{size} * {stride}] = {{
{c_bytes(packed)}
}};
"""
        )
        externs.append(
            f"""#define HIPHI_LOGO_{size}_STRIDE {stride}
/* Host-order RGB565: (R>>3)<<11 | (G>>2)<<5 | (B>>3). NOT byte-swapped. */
extern const uint16_t hiphi_logo_rgb565_{size}[{size} * {size}];
/* 1 bit per pixel, MSB first, {stride} bytes per row. 1 = opaque. */
extern const uint8_t hiphi_logo_alpha_{size}[{size} * {stride}];
"""
        )

    sizes_list = ", ".join(str(s) for s in RGB565_SIZES)
    header = f"""{BANNER}
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

/* HiPhi mark for the M5GFX/LovyanGFX targets, at {sizes_list} px. */

{"".join(externs)}
#ifdef __cplusplus
}}  /* extern "C" */

/*
 * Draw the mark over a solid background colour.
 *
 * Byte order matters here. LovyanGFX picks the source pixel format from
 * `get_depth<T>`: for a plain `uint16_t` that resolves to `rgb565_2Byte`,
 * which is the BYTE-SWAPPED layout, while `lgfx::rgb565_t` is
 * `rgb565_nonswapped` - host order, exactly what this file stores. Passing the
 * arrays as `const uint16_t*` would therefore swap red and blue, so the helper
 * casts to `const lgfx::rgb565_t*`.
 *
 * Templated on the GFX type so this header needs no M5GFX include; instantiate
 * it from a translation unit that already has M5GFX/LovyanGFX in scope.
 *
 * Composites one row at a time into a small stack buffer - no allocation, no
 * sprite, and a single pushImage per row.
 */
template <typename GFX>
inline void hiphi_logo_draw_rgb565(GFX &gfx, int x, int y, int size,
                                   uint32_t background_rgb888) {{
    const uint16_t *pixels = nullptr;
    const uint8_t *alpha = nullptr;
    int stride = 0;
    switch (size) {{
{"".join(
    f"""    case {s}:
        pixels = hiphi_logo_rgb565_{s};
        alpha = hiphi_logo_alpha_{s};
        stride = HIPHI_LOGO_{s}_STRIDE;
        break;
""" for s in RGB565_SIZES)}    default:
        return;
    }}

    const uint16_t bg = static_cast<uint16_t>(
        (((background_rgb888 >> 16) & 0xff) >> 3) << 11 |
        (((background_rgb888 >> 8) & 0xff) >> 2) << 5 |
        ((background_rgb888 & 0xff) >> 3));

    uint16_t row[{max(RGB565_SIZES)}];
    for (int ry = 0; ry < size; ++ry) {{
        const uint8_t *mask = alpha + ry * stride;
        const uint16_t *src = pixels + ry * size;
        for (int rx = 0; rx < size; ++rx) {{
            const bool opaque = (mask[rx >> 3] >> (7 - (rx & 7))) & 1;
            row[rx] = opaque ? src[rx] : bg;
        }}
        gfx.pushImage(x, y + ry, size, 1,
                      reinterpret_cast<const lgfx::rgb565_t *>(row));
    }}
}}
#endif /* __cplusplus */
"""

    source = f"""{BANNER}
#include "assets/hiphi_logo_rgb565.h"

{"".join(blocks)}"""

    write(OUT_DIR / "hiphi_logo_rgb565.h", header)
    write(OUT_DIR / "hiphi_logo_rgb565.c", source)


def gen_mono(master: Image.Image, mono_mode: str) -> None:
    size = MONO_SIZE
    ink = mono_ink_mask(master, size, mono_mode)
    packed, stride = pack_bits(ink)

    header = f"""{BANNER}
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define HIPHI_LOGO_MONO_SIZE {size}
#define HIPHI_LOGO_MONO_STRIDE {stride}

/*
 * Floyd-Steinberg dithered onto white, packed 1 bit per pixel, MSB first,
 * {stride} bytes per row. A set bit means ink (draw the foreground colour);
 * a clear bit means paper (leave the background alone).
 *
 * Used by the Frame's 6-colour ACeP e-ink panel, drawn as EINK_BLACK on the
 * existing EINK_WHITE background inside a refresh the caller already performs.
 */
extern const uint8_t hiphi_logo_mono_{size}[{size} * {stride}];

/** True when pixel (x, y) is ink. Out-of-range coordinates return false. */
static inline int hiphi_logo_mono_pixel(int x, int y) {{
    if (x < 0 || y < 0 || x >= {size} || y >= {size}) return 0;
    return (hiphi_logo_mono_{size}[y * {stride} + (x >> 3)] >> (7 - (x & 7))) & 1;
}}

#ifdef __cplusplus
}}
#endif
"""

    source = f"""{BANNER}
#include "assets/hiphi_logo_mono.h"

const uint8_t hiphi_logo_mono_{size}[{size} * {stride}] = {{
{c_bytes(packed)}
}};
"""
    write(OUT_DIR / "hiphi_logo_mono.h", header)
    write(OUT_DIR / "hiphi_logo_mono.c", source)


# ---------------------------------------------------------------------------
# Preview: decode the generated C back into PNGs
# ---------------------------------------------------------------------------


def parse_array(source: str, name: str) -> list[int]:
    start = source.index(name)
    start = source.index("{", start)
    end = source.index("};", start)
    body = source[start + 1 : end]
    return [int(tok, 16) for tok in body.replace("\n", " ").split(",") if tok.strip()]


def gen_previews() -> None:
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)

    lvgl_src = (OUT_DIR / "hiphi_logo_lvgl.c").read_text()
    for size in LVGL_ARGB_SIZES:
        argb = parse_array(lvgl_src, f"hiphi_logo_{size}_argb8888_map")
        image = Image.new("RGBA", (size, size))
        for i in range(size * size):
            b, g, r, a = argb[i * 4 : i * 4 + 4]
            image.putpixel((i % size, i // size), (r, g, b, a))
        # Flatten onto the Dial's black screen so the alpha edge is visible.
        flat = Image.new("RGB", (size, size), (0, 0, 0))
        flat.paste(image, mask=image.getchannel("A"))
        flat.resize((256, 256), Image.NEAREST).save(
            PREVIEW_DIR / f"logo-lvgl-argb8888-{size}.png"
        )

    size = LVGL_SIZE
    a8 = parse_array(lvgl_src, f"hiphi_logo_mono_{size}_a8_map")
    slate = Image.new("RGB", (size, size), (255, 255, 255))
    for i, a in enumerate(a8):
        if a:
            slate.putpixel((i % size, i // size), (0, 0, 0))
    slate.resize((256, 256), Image.NEAREST).save(PREVIEW_DIR / "logo-lvgl-a8-slate.png")

    rgb_src = (OUT_DIR / "hiphi_logo_rgb565.c").read_text()
    for s in RGB565_SIZES:
        words = parse_array(rgb_src, f"hiphi_logo_rgb565_{s}[")
        mask = parse_array(rgb_src, f"hiphi_logo_alpha_{s}[")
        stride = (s + 7) // 8
        # Composite over the M5 setup-screen background so the edge is honest.
        img = Image.new("RGB", (s, s), (0x08, 0x11, 0x1D))
        for y in range(s):
            for x in range(s):
                if not ((mask[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1):
                    continue
                w = words[y * s + x]
                r = ((w >> 11) & 0x1F) * 255 // 31
                g = ((w >> 5) & 0x3F) * 255 // 63
                b = (w & 0x1F) * 255 // 31
                img.putpixel((x, y), (r, g, b))
        img.resize((256, 256), Image.NEAREST).save(
            PREVIEW_DIR / f"logo-rgb565-{s}.png"
        )

    mono_src = (OUT_DIR / "hiphi_logo_mono.c").read_text()
    bits = parse_array(mono_src, f"hiphi_logo_mono_{MONO_SIZE}[")
    stride = (MONO_SIZE + 7) // 8
    img = Image.new("RGB", (MONO_SIZE, MONO_SIZE), (255, 255, 255))
    for y in range(MONO_SIZE):
        for x in range(MONO_SIZE):
            if (bits[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1:
                img.putpixel((x, y), (0, 0, 0))
    img.resize((256, 256), Image.NEAREST).save(PREVIEW_DIR / "logo-mono-frame.png")

    print(f"previews written to {PREVIEW_DIR.relative_to(ROOT)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mono-mode",
        choices=("highpass", "dither"),
        default="highpass",
        help="1-bit reduction strategy; see mono_ink_mask() for why the plain "
        "Floyd-Steinberg mode is not the default",
    )
    parser.add_argument(
        "--preview",
        action="store_true",
        help="decode the generated C arrays back into PNGs under docs/images/previews/",
    )
    args = parser.parse_args()

    master = load_master()
    gen_lvgl(master, args.mono_mode)
    gen_rgb565(master)
    gen_mono(master, args.mono_mode)
    if args.preview:
        gen_previews()


if __name__ == "__main__":
    main()
