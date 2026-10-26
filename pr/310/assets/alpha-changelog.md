# Alpha v2.8.0-alpha.5 compared with Stable v2.5.2

## Requires UHC v4

Use the latest Unified Hi-Fi Control v4 build with this alpha. Update your UHC server before flashing; the new Library uses v4's browse and playback APIs.

Recommended for this alpha: [UHC v4.0.0-beta.10](https://github.com/open-horizon-labs/unified-hifi-control/releases/tag/v4.0.0-beta.10).

## What's different

- **Zone selection:** keep an available saved zone; otherwise select the first available zone automatically. Recovery offers a direct Zones action if selection cannot complete.
- **Steady ring volume:** queued playback updates and briefly lagging provider values no longer reset the next detent. Local predictions reconcile with server confirmation or a bounded settling period.

- **Current stable SDKs:** ESP-IDF 6.1, LVGL 9.6, Arduino-ESP32 3.3.12 and M5Unified 0.2.28, with updated network, display and board components.

- **Playback recovery:** consistent HTTP error handling, validated zone and playback updates, and reliable refreshes for long artwork URLs.
- **Library and volume fixes:** whole LMS playlists, Favorites, and artist/album letter menus with UHC beta.9. Turning the ring from artwork reveals Now Playing and the volume level.

- **Stable upgrades:** preserve saved Wi-Fi, bridge, zone and display settings from v2.5.2 configurations carrying a legacy version marker.

- **Bridge recovery after wake:** all controllers share a last-working address cache and Wi-Fi readiness lifecycle. A healthy bridge is checked directly before DNS or discovery; a failed discovery refresh no longer drops it.

- **Dial and HALO touch navigation:** artwork-only home, a browsable music library, text library lists, and full-screen Zones and Settings. Touch scrolls naturally; the knob browses on these pages.
- **More controller choices:** Slate, Joy, Tough, Dial Lab, Twist, Remote, Kizz and HALO join Dial and Frame in the shared firmware family.
- **Setup and recovery:** nearby Wi-Fi lists, clearer connection diagnostics, saved-network management and safer update checks. Compatible controllers can pair BLE media remotes.
- **Dial power behavior:** modem sleep, automatic Light-sleep and battery Deep-sleep, plus a separate parking image for its auxiliary ESP32. Power diagnostics record sleep evidence.

## Testing status

All alpha.5 target builds and shared software tests passed. The volume-fixed Dial CI image was flashed on a Waveshare ESP32-S3 knob; its owner confirmed the playback screen, touch navigation and ring response, then confirmed stable volume matching Roon. The final tagged Dial image was flashed and reconnected with its saved settings. Other target images need their own hardware qualification. A battery-life improvement has not been measured.
