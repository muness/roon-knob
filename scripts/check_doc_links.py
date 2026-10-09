#!/usr/bin/env python3
"""Verify that relative Markdown links in tracked docs resolve to real paths.

Checks every tracked Markdown file for inline links and images whose target is a
relative path, and reports any that do not exist on disk. External links,
anchors, and mailto/tel targets are ignored.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
SKIP_PREFIXES = ("http://", "https://", "mailto:", "tel:", "#", "data:")


def markdown_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "*.md"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [ROOT / line for line in result.stdout.splitlines() if line]


def main() -> int:
    broken: list[str] = []
    checked = 0
    for path in markdown_files():
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for line_number, line in enumerate(text.splitlines(), start=1):
            for target in LINK_RE.findall(line):
                if target.startswith(SKIP_PREFIXES) or target.startswith("<"):
                    continue
                cleaned = unquote(target.split("#", 1)[0].split("?", 1)[0])
                if not cleaned:
                    continue
                if cleaned.startswith("/"):
                    resolved = ROOT / cleaned.lstrip("/")
                else:
                    resolved = (path.parent / cleaned).resolve()
                checked += 1
                if not resolved.exists():
                    relative = path.relative_to(ROOT).as_posix()
                    broken.append(f"{relative}:{line_number}: missing link target {target!r}")

    if broken:
        print("Markdown link check FAILED", file=sys.stderr)
        for item in broken:
            print(f"- {item}", file=sys.stderr)
        return 1

    print(f"Markdown link check passed ({checked} relative link(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
