#!/usr/bin/env python3
"""Assert that product naming agrees across manifests, the flasher, and the
release template.

Canonical names come from hiphi.audio and web/flash.html's card headings
(source of truth per docs/meta/decisions and GitHub issue #164). This script
holds that list once and checks that:

  * every web/manifest-*.json "name" field uses the canonical product name
    (allowing a trailing "(Alpha)"/channel-suffix, which manifests already
    use and this check does not police beyond stripping it),
  * web/flash.html's `targetNames` map agrees with the canonical names, and
  * .github/RELEASE_TEMPLATE.md's hardware table first column uses the
    canonical names.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# manifest filename stem -> canonical product name
CANONICAL_NAMES = {
    "manifest-s3": "HiPhi Dial",
    "manifest-knob-aux": "Dial auxiliary parking image",
    "manifest-frame": "HiPhi Frame",
    "manifest-rlcd": "HiPhi Slate",
    "manifest-joy": "HiPhi Joy",
    "manifest-tough": "HiPhi Tough",
    "manifest-m5dial": "HiPhi Dial Lab",
    "manifest-sticks3": "HiPhi Twist",
    "manifest-stopwatch": "HiPhi Remote",
    "manifest-stackchan": "Kizz Playback Companion",
}

# flash.html targetNames key -> short form used there instead of the full
# canonical name, for targets where the flasher's target-filter title and
# button labels intentionally use a shorter form than the manifest/release
# table (e.g. "Kizz" instead of "Kizz Playback Companion"; the full form is
# pinned separately by scripts/check_kizz_identity.py). Every other target
# is still checked against the full canonical name.
TARGET_NAME_SHORT_FORMS = {
    "stackchan": "Kizz",
}

# flash.html targetNames key -> manifest stem, so the same canonical table
# can check both.
TARGET_TO_MANIFEST = {
    "dial": "manifest-s3",
    "knobaux": "manifest-knob-aux",
    "frame": "manifest-frame",
    "rlcd": "manifest-rlcd",
    "joy": "manifest-joy",
    "tough": "manifest-tough",
    "m5dial": "manifest-m5dial",
    "sticks3": "manifest-sticks3",
    "stopwatch": "manifest-stopwatch",
    "stackchan": "manifest-stackchan",
}

# .github/RELEASE_TEMPLATE.md hardware table first column -> manifest stem.
RELEASE_TABLE_TO_MANIFEST = {
    "HiPhi Dial": "manifest-s3",
    "HiPhi Frame": "manifest-frame",
    "HiPhi Slate": "manifest-rlcd",
    "HiPhi Joy": "manifest-joy",
    "HiPhi Tough": "manifest-tough",
    "HiPhi Dial Lab": "manifest-m5dial",
    "HiPhi Twist": "manifest-sticks3",
    "HiPhi Remote": "manifest-stopwatch",
    "Kizz Playback Companion": "manifest-stackchan",
}

ALPHA_SUFFIX_RE = re.compile(r"\s*\((?:Alpha|Beta)\)\s*$")


def strip_channel_suffix(name: str) -> str:
    return ALPHA_SUFFIX_RE.sub("", name).strip()


def check_manifests(errors: list[str]) -> None:
    manifest_dir = ROOT / "web"
    for stem, expected in CANONICAL_NAMES.items():
        manifest_path = manifest_dir / f"{stem}.json"
        if not manifest_path.is_file():
            errors.append(f"missing manifest: {manifest_path.relative_to(ROOT)}")
            continue
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        actual = strip_channel_suffix(data.get("name", ""))
        if actual != expected:
            errors.append(
                f"{manifest_path.relative_to(ROOT)}: name is "
                f"{data.get('name')!r}, expected {expected!r} "
                "(optionally with a trailing channel suffix)"
            )


def check_flash_html(errors: list[str]) -> None:
    flash_path = ROOT / "web" / "flash.html"
    text = flash_path.read_text(encoding="utf-8")
    match = re.search(r"const targetNames = \{(.*?)\};", text)
    if not match:
        errors.append(f"{flash_path.relative_to(ROOT)}: could not find targetNames map")
        return
    entries = dict(
        re.findall(r"(\w+):\s*'([^']*)'", match.group(1))
    )
    for target, stem in TARGET_TO_MANIFEST.items():
        expected = TARGET_NAME_SHORT_FORMS.get(target, CANONICAL_NAMES[stem])
        actual = entries.get(target)
        if actual is None:
            errors.append(f"{flash_path.relative_to(ROOT)}: targetNames missing key {target!r}")
        elif actual != expected:
            errors.append(
                f"{flash_path.relative_to(ROOT)}: targetNames[{target!r}] is "
                f"{actual!r}, expected {expected!r}"
            )


def check_release_template(errors: list[str]) -> None:
    template_path = ROOT / ".github" / "RELEASE_TEMPLATE.md"
    text = template_path.read_text(encoding="utf-8")
    found_names: set[str] = set()
    for line in text.splitlines():
        if not line.startswith("| **"):
            continue
        match = re.match(r"\|\s*\*\*(.+?)\*\*\s*\|", line)
        if not match:
            continue
        found_names.add(match.group(1))

    for expected_name in RELEASE_TABLE_TO_MANIFEST:
        if expected_name not in found_names:
            errors.append(
                f"{template_path.relative_to(ROOT)}: hardware table is missing "
                f"the canonical name {expected_name!r}"
            )

    known = set(RELEASE_TABLE_TO_MANIFEST)
    for name in found_names - known:
        errors.append(
            f"{template_path.relative_to(ROOT)}: hardware table uses "
            f"non-canonical name {name!r}"
        )


def main() -> int:
    errors: list[str] = []
    check_manifests(errors)
    check_flash_html(errors)
    check_release_template(errors)

    if errors:
        print("Product naming inconsistencies found:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Product naming is consistent across manifests, flash.html, and the release template.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
