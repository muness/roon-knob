#!/usr/bin/env python3
"""Fail closed when a browser-flashing manifest can overwrite NVS."""

import argparse
import json
from pathlib import Path
import sys


REQUIRED_OFFSETS = (0x0, 0x8000, 0xD000, 0x10000)


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
        parts = manifest["builds"][0]["parts"]
    except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
        fail(f"cannot read {args.manifest}: {error}")

    if len(parts) != len(REQUIRED_OFFSETS):
        fail(f"expected {len(REQUIRED_OFFSETS)} parts, found {len(parts)}")

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

    if tuple(sorted(offsets)) != REQUIRED_OFFSETS:
        fail("parts must be exactly at 0x0, 0x8000, 0xd000, and 0x10000")

    print(f"flash manifest check passed: {args.manifest} preserves NVS")


if __name__ == "__main__":
    main()
