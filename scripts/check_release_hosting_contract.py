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
require(workflow, "publish_dir: ./publish-root")
require(workflow, "destination_dir: .")
require(workflow, 'DEPLOY_BASE="${{ steps.channel.outputs.url }}"')
require(workflow, '> gh-pages-build/index.html')
require(workflow, 'cp web/index.html publish-root/index.html')
require(workflow, "cp -R web/assets publish-root/assets")
require(workflow, "cp -R web/assets pr-preview/assets")
require(workflow, "cp -R web/assets gh-pages-build/assets")
require(workflow, 'mkdir -p "publish-root/${CHANNEL}"')
require(workflow, 'web/flash.html > "pr-preview/${page}"')
require(workflow, "for legacy_channel in stable beta alpha; do")
require(workflow, '"publish-root/flash/${legacy_channel}/index.html"')
require(workflow, '"https://firmware.hiphi.audio/flash/${legacy_channel}/"')
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
for output in (
    'echo "dir=alpha" >> "$GITHUB_OUTPUT"',
    'echo "url=https://firmware.hiphi.audio/alpha" >> "$GITHUB_OUTPUT"',
    'echo "dir=beta" >> "$GITHUB_OUTPUT"',
    'echo "url=https://firmware.hiphi.audio/beta" >> "$GITHUB_OUTPUT"',
    'echo "dir=stable" >> "$GITHUB_OUTPUT"',
    'echo "url=https://firmware.hiphi.audio/stable" >> "$GITHUB_OUTPUT"',
):
    require(workflow, output)
legacy_release_host = "roon" + "-knob.muness.com"
forbid(workflow, legacy_release_host)
forbid(workflow, "destination_dir: ${{ steps.channel.outputs.dir }}")
forbid(workflow, "destination_dir: flash/${{ steps.channel.outputs.name }}")

for page in ("web/index.html", "web/flash.html"):
    require(page, 'href="./assets/favicon.ico"')
    require(page, 'href="./assets/styles.css"')
    require(page, 'src="./assets/site.js"')
for asset in (
    "web/assets/favicon.ico",
    "web/assets/styles.css",
    "web/assets/site.js",
    "web/assets/fonts/atkinson-hyperlegible-next-latin.woff2",
    "web/assets/fonts/familjen-grotesk-latin.woff2",
    "web/assets/fonts/Atkinson-Hyperlegible-Next-OFL.txt",
    "web/assets/fonts/Familjen-Grotesk-OFL.txt",
):
    if not (ROOT / asset).is_file():
        raise SystemExit(f"{asset}: required owned brand asset is missing")
require("web/assets/styles.css", 'url("./fonts/atkinson-hyperlegible-next-latin.woff2")')
require("web/assets/styles.css", 'url("./fonts/familjen-grotesk-latin.woff2")')
require("web/assets/styles.css", "Firmware-owned fork of https://github.com/open-horizon-labs/hiphi/blob/main/styles.css")
require("web/assets/site.js", "Firmware-owned fork of https://github.com/open-horizon-labs/hiphi/blob/main/site.js")
forbid("web/assets/styles.css", "fonts.googleapis.com")
require("web/index.html", 'href="./stable/"')
require("web/index.html", 'href="./beta/"')
require("web/index.html", 'href="./alpha/"')
require("web/flash.html", 'href="/stable/"')
require("web/flash.html", 'href="/beta/"')
require("web/flash.html", 'href="/alpha/"')
require("web/redirect.html", 'http-equiv="refresh"')
forbid("web/flash.html", legacy_release_host)
if (ROOT / "web/flash-pr.html").exists():
    raise SystemExit("web/flash-pr.html: PR previews must use the shared channel template")

for path in (
    "README.md",
    ".github/RELEASE_TEMPLATE.md",
    "docs/usage/FIRMWARE_FLASHING.md",
    "docs/usage/GETTING_STARTED.md",
    "docs/usage/OTA_UPDATES.md",
):
    forbid(path, "firmware.hiphi.audio/flash.html")
    forbid(path, "firmware.hiphi.audio/alpha/flash.html")
    forbid(path, "firmware.hiphi.audio/beta/flash.html")
    forbid(path, "firmware.hiphi.audio/flash/stable/")
    forbid(path, "firmware.hiphi.audio/flash/alpha/")
    forbid(path, "firmware.hiphi.audio/flash/beta/")

print("release hosting and component-isolation contracts OK")
