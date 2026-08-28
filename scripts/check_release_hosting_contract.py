#!/usr/bin/env python3
"""Fail when firmware component isolation or Pages channel routing regresses."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(path: str, needle: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    if needle not in text:
        raise SystemExit(f"{path}: missing required release contract: {needle}")


def forbid(path: str, needle: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    if needle in text:
        raise SystemExit(f"{path}: retired release contract remains: {needle}")


for legacy_app in ("idf_app", "frame_app", "rlcd_app"):
    require(f"{legacy_app}/CMakeLists.txt", "set(EXCLUDE_COMPONENTS m5_platform kizz_wake_word)")

workflow = ".github/workflows/docker.yml"
require(workflow, "cname: firmware.hiphi.audio")
require(workflow, "destination_dir: flash/${{ steps.channel.outputs.name }}")
require(workflow, 'DEPLOY_BASE="https://firmware.hiphi.audio/flash/${{ steps.channel.outputs.name }}"')
workflow_text = (ROOT / workflow).read_text(encoding="utf-8")
if workflow_text.count("group: firmware-pages") != 2:
    raise SystemExit(
        f"{workflow}: PR and release Pages deployments must share one concurrency group"
    )
for channel, pattern in (
    ("alpha", r"^v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$"),
    ("beta", r"^v[0-9]+\.[0-9]+\.[0-9]+-beta\.[0-9]+$"),
    ("stable", r"^v[0-9]+\.[0-9]+\.[0-9]+$"),
):
    require(workflow, pattern)
    require(workflow, f'echo "name={channel}" >> "$GITHUB_OUTPUT"')
legacy_release_host = "roon" + "-knob.muness.com"
forbid(workflow, legacy_release_host)
forbid(workflow, "destination_dir: beta")

require("web/flash.html", "location.hostname !== 'firmware.hiphi.audio'")
forbid("web/flash.html", legacy_release_host)

print("release hosting and component-isolation contracts OK")
