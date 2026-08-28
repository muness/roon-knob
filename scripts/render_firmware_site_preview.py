#!/usr/bin/env python3
"""Render a PR's firmware-site UI against already-published alpha binaries."""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path


TARGETS = (
    ("dial", "flash-dial.html"),
    ("frame", "flash-frame.html"),
    ("rlcd", "flash-rlcd.html"),
    ("joy", "flash-joy.html"),
    ("tough", "flash-tough.html"),
    ("m5dial", "flash-m5dial.html"),
    ("sticks3", "flash-sticks3.html"),
    ("stopwatch", "flash-stopwatch.html"),
    ("stackchan", "flash-stackchan.html"),
)


def render(template: str, replacements: dict[str, str]) -> str:
    for token, value in replacements.items():
        template = template.replace("{{" + token + "}}", value)

    # Site previews exercise current HTML/CSS while deliberately flashing the
    # last published alpha. Absolute paths keep manifests and downloads on the
    # firmware origin without copying release artifacts into every PR preview.
    template = re.sub(
        r'manifest="(?:\./)?(manifest-[^"]+\.json)"',
        r'manifest="/alpha/\1"',
        template,
    )
    template = re.sub(
        r'href="\./(hiphi_[^"]+\.bin)"',
        r'href="/alpha/\1"',
        template,
    )
    return template


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--web-dir", type=Path, required=True)
    parser.add_argument("--published-alpha-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--pr-number", required=True)
    parser.add_argument("--head-sha", required=True)
    parser.add_argument("--run-url", required=True)
    args = parser.parse_args()

    manifest = json.loads(
        (args.published_alpha_dir / "manifest-frame.json").read_text()
    )
    version = manifest["version"]
    output = args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(args.web_dir / "assets", output / "assets", dirs_exist_ok=True)

    index = (args.web_dir / "index.html").read_text()
    for channel in ("stable", "beta", "alpha"):
        index = index.replace(f'href="./{channel}/"', f'href="/{channel}/"')
    (output / "index.html").write_text(index)

    short_sha = args.head_sha[:12]
    notice = (
        '<div class="channel-notice alpha"><strong>Site-only PR preview.</strong>'
        f'<p>UI from PR #{args.pr_number} commit <code>{short_sha}</code>; '
        f'Flash actions use the already-published alpha v{version} binaries. '
        f'<a href="{args.run_url}">Preview run ↗</a></p></div>'
    )
    common = {
        "VERSION": version,
        "RELEASE_CHANNEL": "alpha",
        "RELEASE_CHANNEL_NAME": "Site preview",
        "RELEASE_CHANNEL_NOTICE": notice,
        "RELEASE_LINK": (
            f'<a href="https://github.com/muness/roon-knob/pull/{args.pr_number}">'
            f'Review PR #{args.pr_number} ↗</a>'
        ),
    }
    template = (args.web_dir / "flash.html").read_text()
    (output / "flash.html").write_text(
        render(template, {**common, "TARGET_FILTER": ""})
    )
    for target, filename in TARGETS:
        (output / filename).write_text(
            render(template, {**common, "TARGET_FILTER": target})
        )

    unresolved = list(output.glob("*.html"))
    for page in unresolved:
        text = page.read_text()
        if re.search(r"\{\{[^}]+\}\}", text):
            raise SystemExit(f"unresolved template token in {page}")
        if 'manifest="./manifest-' in text:
            raise SystemExit(f"relative preview manifest in {page}")


if __name__ == "__main__":
    main()
