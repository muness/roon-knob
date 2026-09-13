#!/usr/bin/env python3
"""Classify a newline-delimited PR file list as firmware-site-only or mixed."""

from __future__ import annotations

import argparse
import sys


SITE_PREFIXES = ("web/", "docs/")
SITE_FILES = {
    "README.md",
    "scripts/check_release_hosting_contract.py",
    "scripts/check_alpha_release_notes.py",
    "scripts/classify_firmware_site_changes.py",
    "scripts/render_firmware_site_preview.py",
    ".github/workflows/docker.yml",
    ".github/workflows/firmware-site-preview.yml",
}


def is_site_only(paths: list[str]) -> bool:
    return bool(paths) and all(
        path in SITE_FILES or path.startswith(SITE_PREFIXES) for path in paths
    )


def self_test() -> None:
    assert is_site_only(["web/index.html", "docs/usage/FIRMWARE_FLASHING.md"])
    assert is_site_only([".github/workflows/firmware-site-preview.yml"])
    assert not is_site_only([])
    assert not is_site_only(["web/index.html", "frame_app/main/main.c"])
    assert not is_site_only(["webish/index.html"])
    print("firmware-site change classifier self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    paths = [line.strip() for line in sys.stdin if line.strip()]
    print("true" if is_site_only(paths) else "false")


if __name__ == "__main__":
    main()
