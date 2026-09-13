#!/usr/bin/env python3
"""Require one Arduino compile mode across the official M5 abstraction stack."""

import json
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_m5_arduino_abi.py COMPILE_COMMANDS", file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    commands = json.loads(path.read_text(encoding="utf-8"))
    required = {
        "M5GFX": "m5gfx/src/M5GFX.cpp",
        "M5Unified": "m5unified/src/M5Unified.cpp",
    }
    failed = False
    for label, suffix in required.items():
        matches = [entry for entry in commands if suffix in entry["file"]]
        if len(matches) != 1 or "-DARDUINO=10812" not in matches[0]["command"]:
            print(f"FAIL: {label} is not compiled with Arduino 10812", file=sys.stderr)
            failed = True

    if failed:
        return 1
    print("M5 official abstraction stack uses one Arduino ABI")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
