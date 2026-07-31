# Firmware Artifacts and Preview Testing

This is the developer contract for Dial and Frame browser-flasher artifacts.
It complements the user-facing [flashing guide](../usage/FIRMWARE_FLASHING.md).

## Settings-preserving update

The normal ESP Web Tools manifest writes four independently addressed parts:

| Part | Offset |
| --- | ---: |
| bootloader | `0x0000` |
| partition table | `0x8000` |
| initial OTA data | `0xd000` |
| application | `0x10000` |

The NVS partition occupies `0x9000` through `0xcfff`. None of the normal
update parts overlaps it, so declining the browser's erase prompt preserves
Wi-Fi credentials and controller settings. The same multi-part contract is
used for stable pages and PR previews.

The merged image remains available only as a destructive factory/clean-install
artifact. Flashing it at offset zero spans NVS; it must never be presented as
the ordinary update path.

## Immutable PR previews

Each successful PR deployment publishes pages, manifests, component files, and
the legacy merged image under a name containing the exact GitHub SHA, Actions
run ID, and attempt. The deployment job:

1. downloads the target build artifacts;
2. creates target-specific and combined flash pages;
3. validates each manifest's offsets and assets, including the NVS exclusion;
4. downloads the deployed files again and byte-compares them with the build
   inputs; and
5. edits the single `Firmware Previews` GitHub Actions comment on the PR with
   Dial, Frame, combined-page, manifest, binary, SHA-256, and size links.

The comment is deliberately updated rather than duplicated. It leads with the
UTC build timestamp, linked Actions run and attempt, PR-head SHA, and commit
subject. The flash page repeats that metadata, including the merge ref used to
build the artifact. A build job completing is not enough: wait for the
dependent **Deploy PR Preview** job to finish before expecting the flasher URL
to change.

## Release gate

Artifact provenance proves the hosted bits match CI, not that a device boots.
Hardware validation remains separate. For Dial, test cold boot and display/UI
loop before Wi-Fi provisioning, then provisioning, reconnection, and BLE media
remote behavior. For Frame, test boot, provisioning, e-ink/input, and BLE.
Never merge solely because browser flashing, a manifest check, or a hosted HTTP
200 succeeded.
