#!/usr/bin/env python3
"""Fail closed when a browser-flashing manifest can overwrite NVS."""

import argparse
import json
from pathlib import Path
import sys


BOOTLOADER_OFFSETS = {
    "ESP32": 0x1000,
    "ESP32-S2": 0x0,
    "ESP32-S3": 0x0,
    "ESP32-C3": 0x0,
    "ESP32-C6": 0x0,
    "ESP32-H2": 0x0,
}
PARTITION_OFFSET = 0x8000
OTA_DATA_OFFSET = 0xD000
APPLICATION_OFFSET = 0x10000


def fail(message: str) -> None:
    print(f"flash manifest check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--assets-dir", type=Path, required=True)
    parser.add_argument("--nvs-offset", type=lambda value: int(value, 0), default=0x9000)
    parser.add_argument("--nvs-size", type=lambda value: int(value, 0), default=0x4000)
    args = parser.parse_args()

    try:
        manifest = json.loads(args.manifest.read_text())
        build = manifest["builds"][0]
        chip_family = build["chipFamily"]
        parts = build["parts"]
    except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
        fail(f"cannot read {args.manifest}: {error}")

    bootloader_offset = BOOTLOADER_OFFSETS.get(chip_family)
    if bootloader_offset is None:
        fail(f"unsupported chip family: {chip_family}")
    if len(parts) == 3:
        # Factory/single-app images have no otadata partition.
        required_offsets = (
            bootloader_offset,
            PARTITION_OFFSET,
            APPLICATION_OFFSET,
        )
    elif len(parts) == 4:
        required_offsets = (
            bootloader_offset,
            PARTITION_OFFSET,
            OTA_DATA_OFFSET,
            APPLICATION_OFFSET,
        )
    else:
        fail(f"expected 3 factory parts or 4 OTA parts, found {len(parts)}")

    offsets = []
    nvs_end = args.nvs_offset + args.nvs_size
    for part in parts:
        try:
            path = part["path"]
            offset = int(part["offset"])
        except (KeyError, TypeError, ValueError) as error:
            fail(f"invalid part: {error}")
        if Path(path).is_absolute() or ".." in Path(path).parts:
            fail(f"part path must be a local asset name: {path}")
        asset = args.assets_dir / path
        if not asset.is_file():
            fail(f"missing manifest asset: {asset}")
        end = offset + asset.stat().st_size
        if offset < nvs_end and end > args.nvs_offset:
            fail(f"{path} range 0x{offset:x}-0x{end - 1:x} overlaps NVS")
        offsets.append(offset)

    if tuple(sorted(offsets)) != tuple(sorted(required_offsets)):
        expected = ", ".join(f"0x{offset:x}" for offset in required_offsets)
        fail(f"{chip_family} parts must be exactly at {expected}")

    print(f"flash manifest check passed: {args.manifest} preserves NVS")


if __name__ == "__main__":
    main()
