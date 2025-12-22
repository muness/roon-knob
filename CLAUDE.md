# Claude Agent Instructions

See [PROJECT_AIMS](./docs/meta/PROJECT_AIMS.md) for this project's aims. See [decisions](./docs/meta/decisions/) for decisions we've made along the way.

See [AGENTS.md](AGENTS.md) for complete instructions on working with this project.

## Documentation by Area

| Area | Path | Description |
|------|------|-------------|
| **Usage** | `docs/usage/` | End-user guides: WiFi setup, OTA updates, Bluetooth mode |
| **Dev** | `docs/dev/` | Developer reference: build, boot sequence, FreeRTOS, NVS storage |
| **ESP** | `docs/esp/` | Hardware specifics: display, touch, encoder, battery, BLE HID |
| **Meta** | `docs/meta/` | Project aims, roadmap ideas, architectural decisions |
| **Howto** | `docs/howto/` | Tutorials: porting to other boards, reusing patterns |

**When working on:**
- **UI changes** → `docs/esp/DISPLAY.md`, `docs/esp/TOUCH_INPUT.md`, `docs/esp/FONTS.md`
- **Input handling** → `docs/esp/ROTARY_ENCODER.md`, `docs/esp/SWIPE_GESTURES.md`
- **WiFi/networking** → `docs/usage/WIFI_PROVISIONING.md`, `docs/dev/NVS_STORAGE.md`
- **Bluetooth** → `docs/esp/BLE_HID.md`, `docs/usage/DUAL_CHIP_ARCHITECTURE.md`
- **Build/config** → `docs/dev/KCONFIG.md`, `docs/dev/DEVELOPMENT.md`
- **Architecture decisions** → `docs/meta/decisions/`

**Keeping docs current:** When you learn something new about the hardware (pin mappings, component behavior, timing), update the relevant file in `docs/esp/hw-reference/`.

## Key Points

- This project uses **Beads (bd)** for all task and issue management
- Initialize once per clone: `bd init --quiet`
- Core workflow: check `bd ready`, claim tasks, sync when done
- Always run `bd sync` at the end of a session
- No external trackers, no Markdown TODOs

## Roadmap-Driven Development

The [README.md](README.md#roadmap) contains the project roadmap with release blockers and future improvements. When starting work:

1. **Check the roadmap** in README.md to understand priorities
2. **Check beads** for existing tasks: `bd ready`
3. **Create beads tasks** for roadmap items if they don't exist yet
4. **Break down large features** into fine-grained subtasks using beads parent-child relationships
5. **Update roadmap status** in README.md when features are completed

### Example Workflow

```bash
# Check what's ready to work on
bd ready

# If implementing a roadmap feature, create parent task first
bd create "WiFi Provisioning (SoftAP)" -t epic -p 0 --json

# Break into subtasks
bd create "Add SoftAP mode to wifi_manager" -t task -p 1 --deps parent-child:bd-XX
bd create "Create captive portal HTML" -t task -p 1 --deps parent-child:bd-XX
bd create "Add provisioning timeout logic" -t task -p 1 --deps parent-child:bd-XX

# Claim and work on a subtask
bd update bd-YY --status in_progress

# Complete and sync
bd close bd-YY --reason "implemented"
bd sync
```

### Keeping Roadmap in Sync

- When a roadmap feature is **fully complete**, update its status in README.md from "Not started" to "Complete"
- When work is **in progress**, update to "In progress"
- Beads is the source of truth for task details; README roadmap is the high-level view

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
gh pr create --fill   # or with custom title/body

# 4. Wait for CI to pass, then merge
gh pr merge --squash --delete-branch
```

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
- Injects it into `idf_app/CMakeLists.txt` (ESP32-S3 firmware)
- Injects it into `roon-extension/package.json` (Bridge)
- Builds all firmware and Docker images
- Creates GitHub release with binaries

**Version locations (DO NOT EDIT MANUALLY):**
- `idf_app/CMakeLists.txt` → `PROJECT_VER` (injected by CI)
- `roon-extension/package.json` → `version` (injected by CI)
- `esp32_bt/` → No version (rarely changes)

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
This project uses ESP-IDF v5.4.3. The local install is symlinked at `~/esp/esp-idf`.

### Target Chip
The target is ESP32-S3, not ESP32. If you get weird errors, check:
```bash
idf.py set-target esp32s3
```
