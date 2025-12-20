# Roadmap Ideas

User feedback and feature ideas collected from the Roon community. See [PROJECT_AIMS.md](PROJECT_AIMS.md) for how these connect to project goals and the decision framework for prioritization.

---

## Current User Feedback Summary

As of December 20 2025, I have received feedback through the Roon Labs community forum.

### Setup & First-Run Issues

1. **Flash address confusion** — User flashed at 0x0 instead of 0x10000, corrupted bootloader. Recovered by reflashing Waveshare stock firmware first. *Suggestion: Include bootloader+partition table in release, or merged firmware.*

2. **mDNS unreliability** — Extension discovery didn't work. Had to manually configure bridge URL.

3. **Bridge URL discoverability** — The `http://knob_ip` config page exists but users couldn't find it. Settings menu doesn't expose it clearly.

4. **Setup complexity** (gTunes) — Multiple separate experiences: captive portal, bridge setup, Roon authorization. Waveshare's on-device WiFi setup "took seconds." *Question: Why URLs instead of IP+port? Why not configure on device?*

### Display & Configuration Requests

5. **180° display rotation** (Foo_Bar) — USB cable comes from bottom; would prefer top. For desk placement with USB power.

6. **Always-on display** (Foo_Bar) — At least while music playing. Battery not a concern for USB-powered setups.

7. **Configurable dim/sleep timing** — Related to always-on request.

### Touch UX Issues

8. **Tap targets too small** (gTunes) — Play/pause/skip buttons need more space/padding.

9. **Wake-then-tap behavior** (gTunes) — First tap both wakes screen AND registers as tap. Should just wake (like phones).

10. **Tap-anywhere-to-play/pause** (gTunes) — Most common operation should be easiest. Tap middle of knob without looking for exact button.

11. **Settings icon needed** (gTunes) — No obvious way to access settings.

### Multi-Knob & Zone Features

12. **Multiple knobs work** (Trebz) — Confirmed working. Each knob sees all zones.

13. **Zone filtering per knob** — Allow deselecting zones so each knob controls a subset.

### Architecture Ideas

14. **Device-as-extension** (gTunes) — Eliminate bridge entirely; knob talks directly to Roon. *Trade-off: battery life. Counter-proposal: "caffeine mode" for always-on vs deep-sleep.*

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
- [ ] **Powered use** Configurable dimming and sleep
- [ ] **Settings icon** — Visible affordance for settings access
- [ ] **Display rotation (180°)** — 90/180/270 + Always | When powered | Never. From device or via Bridge advanced settings UX?

### Evaluate with Users: Configuration Options

- [ ] **Always-on / configurable sleep** — Understand demand before implementing
- [ ] **Zone filtering per knob** — Nice-to-have, needs UI design

### Park for now

- [-] **Device-as-extension** — I don't think this is even feasible on an embedded device, not a fit for my desires for controllers.
