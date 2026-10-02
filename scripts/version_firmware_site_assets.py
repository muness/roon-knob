#!/usr/bin/env python3
"""Give generated firmware pages content-addressed CSS/JS URLs."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def version_assets(directory: Path) -> None:
    replacements = {}
    for name in ("styles.css", "site.js"):
        asset = directory / "assets" / name
        content = asset.read_bytes()
        digest = hashlib.sha256(content).hexdigest()[:16]
        versioned = f"{asset.stem}-{digest}{asset.suffix}"
        (asset.parent / versioned).write_bytes(content)
        replacements[f"./assets/{name}"] = f"./assets/{versioned}"
    for page in directory.glob("*.html"):
        content = page.read_text(encoding="utf-8")
        for source, target in replacements.items():
            content = content.replace(source, target)
        page.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    version_assets(parser.parse_args().directory)
