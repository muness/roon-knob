# Alpha v2.8.0-alpha.2 compared with Stable v2.5.2

## Requires UHC v4

Use the latest Unified Hi-Fi Control v4 build with this alpha. Update your UHC server before flashing; the new Library uses v4's browse and playback APIs.

Recommended for this alpha: [UHC v4.0.0-beta.4](https://github.com/open-horizon-labs/unified-hifi-control/releases/tag/v4.0.0-beta.4).

## What's different

- **Stable upgrades:** preserve saved Wi-Fi, bridge, zone and display settings from v2.5.2 configurations carrying a legacy version marker.

- **Bridge recovery after wake:** all controllers share a last-working address cache and Wi-Fi readiness lifecycle. A healthy bridge is checked directly before DNS or discovery; a failed discovery refresh no longer drops it.

- **Dial and HALO touch navigation:** artwork-only home, a browsable music library, album covers, and full-screen Zones and Settings. Touch scrolls naturally; the knob browses on these pages.
- **More controller choices:** Slate, Joy, Tough, Dial Lab, Twist, Remote, Kizz and HALO join Dial and Frame in the shared firmware family.
- **Setup and recovery:** nearby Wi-Fi lists, clearer connection diagnostics, saved-network management and safer update checks. Compatible controllers can pair BLE media remotes.
- **Dial power behavior:** modem sleep, automatic Light-sleep and battery Deep-sleep, plus a separate parking image for its auxiliary ESP32. Power diagnostics record sleep evidence.

## Testing status

The Dial touch preview was tested on hardware. Exact alpha images and other controllers still need their own hardware checks. A battery-life improvement has not been measured.
