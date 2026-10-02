# Claude Agent Instructions

See [PROJECT_AIMS](./docs/meta/PROJECT_AIMS.md) for this project's aims. See [decisions](./docs/meta/decisions/) for decisions we've made along the way.

See [AGENTS.md](AGENTS.md) for complete instructions on working with this project.

## Documentation by Area

| Area | Path | Description |
|------|------|-------------|
| **Usage** | `docs/usage/` | End-user guides: Dial setup, WiFi provisioning, OTA updates |
| **Targets** | `docs/targets/` | Per-controller index: what is documented for each target |
| **Dial** | `docs/dial/` | Dial and shared ESP32 hardware: display, touch, encoder, battery |
| **Dev** | `docs/dev/` | Developer reference: build, boot sequence, FreeRTOS, NVS storage |
| **Meta** | `docs/meta/` | Project aims, roadmap ideas, architectural decisions |
| **Howto** | `docs/howto/` | Tutorials: porting to other boards, reusing patterns |

**Per-target docs:**

| Controller | Target dir | Docs |
|---|---|---|
| HiPhi Dial | `idf_app` | `docs/usage/DIAL.md`, `docs/dial/` |
| Dial auxiliary parking image | `knob_aux_app` | `docs/dial/DUAL_CHIP_ARCHITECTURE.md` |
| HiPhi Frame | `frame_app` | `docs/dial/BLE_HID.md`, `.oh/frame-recovery.md` |
| HiPhi Slate | `rlcd_app` | `docs/targets/README.md` |
| HiPhi Joy | `atom_app` | `docs/dial/hw-reference/board-atom-s3-joystick.md` |
| HiPhi Tough | `tough_app` | `docs/dial/M5STACK.md`, `docs/dial/hw-reference/board-tough.md` |
| Dial Lab / Twist / Remote / Kizz | `m5_beta_app` | `docs/dial/hw-reference/m5-form-native-betas.md`, `docs/dev/KIZZ_VOICE.md` |

**When working on:**
- **UI changes** → `docs/dial/DISPLAY.md`, `docs/dial/TOUCH_INPUT.md`, `docs/dial/FONTS.md`
- **Input handling** → `docs/dial/ROTARY_ENCODER.md`, `docs/dial/SWIPE_GESTURES.md`
- **WiFi/networking** → `docs/usage/WIFI_PROVISIONING.md`, `docs/dev/NVS_STORAGE.md`
- **Build/config** → `docs/dev/KCONFIG.md`, `docs/dev/DEVELOPMENT.md`
- **Architecture decisions** → `docs/meta/decisions/`
- **A target that is not the Dial** → `docs/targets/README.md` first

**Keeping docs current:** When you learn something new about the hardware (pin mappings, component behavior, timing), update the relevant file in `docs/dial/hw-reference/`.

## Key Points

- This project uses **GitHub Issues** for all task and issue management
- No Markdown TODOs — create GitHub issues instead

## Roadmap-Driven Development

The [README.md](README.md#roadmap) contains the project roadmap with release blockers and future improvements. When starting work:

1. **Check the roadmap** in README.md to understand priorities
2. **Check GitHub Issues** for existing tasks: `gh issue list`
3. **Create GitHub issues** for roadmap items if they don't exist yet
4. **Break down large features** into subtasks (use task lists in the parent issue)
5. **Update roadmap status** in README.md when features are completed

### Example Workflow

```bash
# Check what's ready to work on
gh issue list

# If implementing a roadmap feature, create a tracking issue
gh issue create --title "WiFi Provisioning (SoftAP)" --body "Tracking issue for WiFi provisioning"

# Create subtask issues
gh issue create --title "Add SoftAP mode to wifi_manager"
gh issue create --title "Create captive portal HTML"
gh issue create --title "Add provisioning timeout logic"

# Work on a subtask, then close when done
gh issue close <number> -c "Implemented in #<PR>"
```

### Keeping Roadmap in Sync

- When a roadmap feature is **fully complete**, update its status in README.md from "Not started" to "Complete"
- When work is **in progress**, update to "In progress"
- GitHub Issues is the source of truth for task details; README roadmap is the high-level view

## Git Workflow (MANDATORY)

**NEVER push directly to master.** The `master` branch is protected. Always use feature branches and PRs.

### For Every Change:

```bash
# 1. Create a feature branch from master
git checkout master
git pull origin master
git checkout -b fix/short-description   # or feature/short-description

# 2. Make your changes and commit
git add <files>
git commit -m "Description of change"

# 3. Push the branch and create a PR
git push -u origin fix/short-description
gh pr create --draft --fill   # or with custom title/body

# 4. Wait for CI to pass, then merge
gh pr merge --squash --delete-branch
```

For significant changes, follow the `/solution-space → /execute → /ship`
workflow and the `/review` + `/dissent` gates in `AGENTS.md`.

### Branch Naming

- `fix/` - Bug fixes (e.g., `fix/wifi-credential-persistence`)
- `feature/` - New features (e.g., `feature/bluetooth-mode`)
- `docs/` - Documentation only (e.g., `docs/bluetooth-readme`)

### Why This Matters

- CI runs on PRs to catch build failures before merge
- Branch protection prevents accidental force-pushes to master
- PR history provides clear audit trail of changes

### Merging and Releasing

**ALWAYS test before shipping.** Never ask "test or ship?" - the answer is ALWAYS test first. Build the firmware, let the user flash and verify it works, then commit/merge/release.

**ALWAYS ASK the user before:**
- Merging a PR (`gh pr merge`)
- Tagging a release (`git tag`)
- Pushing tags (`git push origin v*`)

The user needs to test locally first. Never assume a fix works - wait for explicit confirmation.

To cut a release, just create and push a tag. **Do NOT manually edit version numbers** - the CI handles everything:

```bash
# Tag the release (from master, AFTER user confirms testing)
git tag -a v1.X.Y -m "Release description"
git push origin v1.X.Y
```

The GitHub Actions workflow (`docker.yml`) automatically:
- Extracts version from the tag name
- Injects it into every target's `CMakeLists.txt`
- Builds firmware and creates GitHub release with binaries
- Deploys web flasher to GitHub Pages

**Version locations (DO NOT EDIT MANUALLY).** CI injects `PROJECT_VER` into each
of these in `docker.yml`:
- `idf_app/CMakeLists.txt` (HiPhi Dial)
- `frame_app/CMakeLists.txt` (HiPhi Frame)
- `rlcd_app/CMakeLists.txt` (HiPhi Slate)
- `atom_app/CMakeLists.txt` (HiPhi Joy)
- `tough_app/CMakeLists.txt` (HiPhi Tough)
- `m5_beta_app/CMakeLists.txt` (Dial Lab, Twist, Remote, Kizz)
- `knob_aux_app/CMakeLists.txt` (Dial auxiliary parking image)

**Bridge:** [unified-hifi-control](https://github.com/open-horizon-labs/unified-hifi-control)

## Common Pitfalls (READ THIS FIRST)

**Before struggling with build/config issues, READ THE PROJECT DOCS:**

### sdkconfig Changes Don't Apply
When adding options to `sdkconfig.defaults`, you MUST delete `sdkconfig`:
```bash
rm sdkconfig
idf.py build
```
`idf.py reconfigure` and `idf.py fullclean` do NOT regenerate sdkconfig from defaults.
See [docs/dev/KCONFIG.md](docs/dev/KCONFIG.md) for details.

### ESP-IDF Version
CI builds every target with ESP-IDF `v5.5.5` (`esp_idf_version` in
`.github/workflows/docker.yml`). Use that exact version locally; the install is
symlinked at `~/esp/esp-idf`. Version changes are explicit migration work, not
a local convenience.

### Fresh Checkout / Worktree Setup
```bash
git submodule update --init --recursive   # M5 vendor components under components/m5_official/vendor
source ~/esp/esp-idf/export.sh
```
Without the submodules, `atom_app` and `m5_beta_app` fail at configure time.

### Target Chip
Targets differ per app. Each `sdkconfig.defaults` pins its chip so a fresh
checkout configures correctly:

| App | Chip |
|-----|------|
| `idf_app`, `frame_app`, `rlcd_app`, `atom_app`, `m5_beta_app` | `esp32s3` |
| `tough_app`, `knob_aux_app` | `esp32` |

If a build fails with undeclared `ESP_EXT1_WAKEUP_*` or similar, a stale
`sdkconfig` was generated for the wrong chip:
```bash
rm sdkconfig
idf.py set-target esp32s3   # or esp32 for tough_app / knob_aux_app
```
