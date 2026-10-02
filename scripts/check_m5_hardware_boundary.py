#!/usr/bin/env python3
"""Reject product-owned M5 hardware drivers and raw bus/pin access.

HiPhi owns interaction semantics and presentation. M5Unified/M5GFX and the
target-specific M5Stack libraries own the physical board mechanisms. Upstream
vendor trees are intentionally excluded from this policy check.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OWNED_ROOTS = (
    ROOT / "components" / "m5_platform",
    ROOT / "m5_beta_app" / "main",
    ROOT / "atom_app" / "main",
    ROOT / "tough_app" / "main",
)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

FORBIDDEN = {
    "direct ESP pin/bus driver include": re.compile(
        r"#\s*include\s*[<\"]driver/(?:gpio|uart|i2c|i2s(?:_std)?)\.h[>\"]"
    ),
    "direct ESP pin/bus driver call": re.compile(
        r"\b(?:gpio|uart|i2c|i2s)_[a-zA-Z0-9_]+\s*\("
    ),
    "Arduino pin access": re.compile(
        r"\b(?:pinMode|digitalRead|digitalWrite|analogRead|analogWrite|ledc[A-Z][A-Za-z0-9_]*)\s*\("
    ),
    "Arduino Wire access": re.compile(r"\bWire[0-9]*\."),
    "M5Unified raw I2C access": re.compile(r"\bM5\.(?:In_I2C|Ex_I2C)\."),
    "copied StackChan transport": re.compile(r"\b(?:stackchan_ref|SCSCL|SCSerial)\b"),
}

REQUIRED_OFFICIAL_APIS = {
    "M5Unified display": "M5.Display",
    "M5Unified touch": "M5.Touch",
    "M5Unified power": "M5.Power",
    "M5Unified microphone": "M5.Mic",
    "M5Dial encoder": "M5Dial.Encoder",
    "Atom JoyStick library": "s_atom_joystick.getJoy1ADCValueX",
    "StackChan BSP motion": "M5StackChan.Motion",
}


def source_files() -> list[Path]:
    files: list[Path] = []
    for root in OWNED_ROOTS:
        if not root.exists():
            continue
        files.extend(
            path for path in root.rglob("*") if path.suffix in SOURCE_SUFFIXES
        )
    return sorted(files)


def main() -> int:
    failures: list[str] = []
    for path in source_files():
        text = path.read_text(encoding="utf-8")
        for label, pattern in FORBIDDEN.items():
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{path.relative_to(ROOT)}:{line}: {label}: {match.group(0)!r}"
                )

    platform_source = (ROOT / "components" / "m5_platform" / "m5_platform.cpp").read_text(
        encoding="utf-8"
    )
    for label, token in REQUIRED_OFFICIAL_APIS.items():
        if token not in platform_source:
            failures.append(f"m5_platform is missing required {label} API: {token}")

    if failures:
        print("M5 hardware boundary check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "M5 hardware boundary OK: product code uses official M5Stack APIs; "
        "no product-owned pin, bus, or servo transport found."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
