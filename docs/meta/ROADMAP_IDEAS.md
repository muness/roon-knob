# Roadmap Ideas

User feedback and feature ideas collected from the Roon community. See [PROJECT_AIMS.md](PROJECT_AIMS.md) for how these connect to project goals and the decision framework for prioritization.

---

## Current User Feedback Summary

As of December 22 2025, I have received feedback through the Roon Labs community forum.

### Setup & First-Run Issues

1. **Flash address confusion** — User flashed at 0x0 instead of 0x10000, corrupted bootloader. Recovered by reflashing Waveshare stock firmware first. *Suggestion: Include bootloader+partition table in release, or merged firmware.*

2. **mDNS unreliability** — Extension discovery didn't work. Had to manually configure bridge URL.

3. **Bridge URL discoverability** — The `http://knob_ip` config page exists but users couldn't find it. Settings menu doesn't expose it clearly.

4. **Setup complexity** (gTunes) — Multiple separate experiences: captive portal, bridge setup, Roon authorization. Waveshare's on-device WiFi setup "took seconds." *Question: Why URLs instead of IP+port? Why not configure on device?*

21. **Bridge IP configuration in knob settings** (Frank_M) — When mDNS fails, manually setting bridge IP is difficult. Suggestion: assume same subnet (first 3 octets) and provide way to set just the last octet in knob settings. [#47](https://github.com/muness/roon-knob/issues/47)

### Display & Configuration Requests

5. **180° display rotation** (Foo_Bar) — USB cable comes from bottom; would prefer top. For desk placement with USB power.

6. **Always-on display** (Foo_Bar) — At least while music playing. Battery not a concern for USB-powered setups.

7. **Configurable dim/sleep timing** — Related to always-on request.

18. **Track progress circle not updating** (Frank_M) — Inner circle (track progress indicator) not updating during playback. Needs investigation. [#44](https://github.com/muness/roon-knob/issues/44)

19. **Display off when zone not playing** (Foo_Bar) — Like RoPieee: display turns off when zone isn't playing. Good visual signal that playback is stopped, saves power. Could be sub-option under Always On mode. [#45](https://github.com/muness/roon-knob/issues/45)

20. **Volume display format option** (Frank_M) — Some devices use 0-96 scale where 96=0dB, making "dB" suffix confusing. Request to show volume as plain number (e.g., "40") instead of "40dB". [#46](https://github.com/muness/roon-knob/issues/46)

### Touch UX Issues

8. **Tap targets too small** (gTunes) — Play/pause/skip buttons need more space/padding.

9. **Wake-then-tap behavior** (gTunes) — First tap both wakes screen AND registers as tap. Should just wake (like phones).

10. **Tap-anywhere-to-play/pause** (gTunes) — Most common operation should be easiest. Tap middle of knob without looking for exact button.

11. **Settings icon needed** (gTunes) — No obvious way to access settings.

12. **Swipe gestures don't adapt to rotation** (Foo_Bar) — After 180° rotation, swipe-up (to access Art Mode) becomes swipe-down. Gesture coordinates should transform with rotation setting. [#43](https://github.com/muness/roon-knob/issues/43)

### Multi-Knob & Zone Features

13. **Multiple knobs work** (Trebz) — Confirmed working. Each knob sees all zones.

14. **Zone filtering per knob** — Allow deselecting zones so each knob controls a subset.

### Architecture Ideas

15. **On-device bridge config** (gTunes) — Device discovers bridge, if not found, prompts for IP+port. Pre-fill with device's first 3 IP octets.

### DevOps

16. **Docker latest tag consistency** — Not tagging consistently breaks watchtower auto-updates.

---

## Recommended Priority Order

Based on Aim 1 (force-multiplier through reduced friction) and Aim 4 (margin protection):

### Do First: Unblock Adoption

- [ ] **Merged firmware binary** — Include bootloader+partition table so flashing at 0x0 works
- [ ] **Docker latest tag consistency** — Confirm + fix tagging
- [ ] **Bridge URL discoverability and config** — Add to settings menu

### Do Next: Daily Use Polish

- [ ] **Wake-without-action** — First tap just wakes screen
- [ ] **Larger tap targets** — More padding on play/pause/skip
- [ ] **Settings icon** — Visible affordance for settings access
- [ ] **Display rotation (180°)** — 90/180/270 + Always | When powered | Never. From device or via Bridge advanced settings UX?
- [ ] **Always-on / configurable sleep** — Configurable dimming and sleep

### Evaluate with Users: Configuration Options

- [ ] **Zone filtering per knob** — Nice-to-have, needs UI design
