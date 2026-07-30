# Development Guide

## Architecture

Roon Knob has three components:

| Component | Description |
|-----------|-------------|
| **ESP32-S3 Firmware** | LVGL-based UI on a 360×360 round LCD. Polls the bridge for now-playing data and sends control commands. |
| **PC Simulator** | SDL2 desktop build that mirrors the firmware UI for rapid development without hardware. |
| **Bridge** | Node.js service that connects to Roon/HiFi systems. See [unified-hifi-control](https://github.com/cloud-atlas-ai/unified-hifi-control). |

## Repository Structure

```
roon-knob/
├── idf_app/           # ESP32-S3 firmware (ESP-IDF)
│   ├── main/          # Application code
│   ├── components/    # Custom ESP-IDF components
│   └── sdkconfig.defaults
├── common/            # Shared code between firmware and simulator
│   ├── ui.c           # LVGL UI implementation
│   ├── bridge_client.c  # HTTP client for bridge API
│   └── app_main.c     # Main application logic
├── pc_sim/            # LVGL + SDL2 simulator
├── web/               # Web flasher (deployed to GitHub Pages)
├── scripts/           # Build and setup helpers
└── docs/              # Documentation
```

## PC Simulator

The simulator lets you develop UI without flashing hardware.

### Setup (macOS)

```bash
./scripts/setup_mac.sh  # Installs cmake, ninja, sdl2, curl via Homebrew
```

### Run

```bash
./scripts/run_pc.sh     # Builds and runs simulator
```

The simulator expects the bridge at `http://127.0.0.1:8088`. Run the bridge from the [unified-hifi-control](https://github.com/cloud-atlas-ai/unified-hifi-control) repo.

### Simulator Controls

| Key | Action |
|-----|--------|
| ↑/↓ or ←/→ | Volume |
| Space/Enter | Play/pause |
| Z or M | Zone picker |

## Firmware Development

### Prerequisites

1. [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) installed (typically to `~/esp/esp-idf`)
2. USB-C cable connected to the knob

### Build and Flash

```bash
export IDF_PATH=~/esp/esp-idf
source "$IDF_PATH/export.sh"

cd idf_app
idf.py build
idf.py flash -p /dev/cu.usbmodem*  # Adjust port for your system
idf.py monitor -p /dev/cu.usbmodem*  # View logs (Ctrl+] to exit)
```

Or use the helper script:

```bash
./scripts/build_flash_idf.sh /dev/cu.usbmodem101
```

**Troubleshooting:** If you get "No serial data received", retry a few times or try another cable.

### Releasing Firmware

Releases are handled via CI. Just tag and push:

```bash
git tag -a v1.2.7 -m "Release description"
git push origin v1.2.7
```

The CI builds firmware, creates a GitHub release, and deploys the web flasher.

## Bridge Development

Bridge code is at [unified-hifi-control](https://github.com/cloud-atlas-ai/unified-hifi-control).

## Hardware

The target is the [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8).

**Do not restate hardware facts here.** The canonical, provenance-classed identity record —
exact part numbers, panel technology and driver IC, colour depth, memory sizes, backlight,
touch, encoders, and the list of facts still awaiting physical verification — is
[docs/esp/hw-reference/board.md](../esp/hw-reference/board.md). Pin mappings are in
[HARDWARE_PINS.md](../esp/hw-reference/HARDWARE_PINS.md).

One behavioural note that matters when you build a UI: this firmware reads **no** physical
button. Rotation comes from the encoder; every button-like action comes from the touchscreen.

### Checking documentation identity claims

After editing Markdown, or after editing the hardware sources the canonical record cites, run:

```bash
python3 scripts/check_docs_identity.py --self-check
```

Host-only: Python standard library, no ESP-IDF, no network, no writes. It is deliberately
not wired into `scripts/ci_sanity.sh` — that script's next step is `idf.py build`, so it cannot run
on a host without a toolchain, which is exactly the host this command is for. Run it directly.
`.github/workflows/docs-identity.yml` runs the same command in CI, together with
`scripts/test_check_docs_identity.sh`.

What it does and does not do:

- It is a **phrase tripwire** for the specific identity contradictions that
  [#211](https://github.com/muness/roon-knob/issues/211) corrected. A novel wording will pass, so a
  green run is not a substitute for reviewing identity claims on their merits.
- It checks that the source files the canonical record's repository-observed rows cite still
  contain the text those rows were read from. That is a freshness check on the citation, not
  evidence the fact is true — and it does **not** check the line numbers those rows cite, so a
  citation can go stale from line movement alone with the check still green.
- If a firmware edit fails a source anchor, **re-derive the row and rewrite the token** — including
  when the edit preserved the fact (writing `.freq_hz = 5000` as `5 * 1000`, reordering a Kconfig
  stanza). Never delete an anchor to make the run green; that removes the evidence instead of
  refreshing it. See the *Consequences* section of the
  [target-identity decision record](../meta/decisions/2026-07-30_DECISION_TARGET_IDENTITY_PROVENANCE.md).
- **Quote source in a fenced block, not by indenting it.** Fenced code is exempt; a four-space
  indented block is **scanned as prose**, so an indented quotation of a superseded wording is
  reported as a claim. That is deliberate — an indentation-only exemption would also exempt the
  reflowed middle of a paragraph — and every violation report says so, so you do not have to
  remember it.
- Suppressions (`RK-IDENT-SUPPRESSED`) are informational and never fail the run. The fixture suite
  pins the exact set as `rule|file|suppressor|count`, so adding a suppressed sentence — even under an
  excuse the baseline already lists — is a review conversation rather than a silent narrowing.

## Task Management

We use [GitHub Issues](https://github.com/muness/roon-knob/issues) for task tracking:

```bash
gh issue list                          # List open issues
gh issue create --title "Description"  # Create a new issue
gh issue close <number> -c "Done"      # Close when complete
```
