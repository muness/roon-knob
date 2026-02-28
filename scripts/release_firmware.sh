#!/bin/bash
set -euo pipefail

# Release firmware - bumps version, commits, tags, and pushes
# GitHub Actions builds the firmware and creates the release automatically
#
# Usage: ./scripts/release_firmware.sh <version>
# Example: ./scripts/release_firmware.sh 1.2.0

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
CMAKE_FILE="$ROOT_DIR/esp_dial/CMakeLists.txt"

show_usage() {
    CURRENT=$(grep 'set(PROJECT_VER' "$CMAKE_FILE" | sed 's/.*"\(.*\)".*/\1/')
    echo "Current version: $CURRENT"
    echo ""
    echo "Usage: $0 <version>"
    echo ""
    echo "Example:"
    echo "  $0 1.2.0"
    echo ""
    echo "This will:"
    echo "  1. Update version in CMakeLists.txt"
    echo "  2. Commit the change"
    echo "  3. Create and push a git tag"
    echo "  4. GitHub Actions will build and create the release"
    exit 1
}

if [ $# -lt 1 ]; then
    show_usage
fi

NEW_VERSION="$1"

# Validate version format (semver with optional pre-release)
if ! echo "$NEW_VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$'; then
    echo "Error: Version must be semver format (e.g., 1.2.3 or 1.2.3-beta.1)"
    exit 1
fi

# Check for uncommitted changes
if ! git -C "$ROOT_DIR" diff --quiet || ! git -C "$ROOT_DIR" diff --cached --quiet; then
    echo "Error: You have uncommitted changes. Please commit or stash them first."
    exit 1
fi

# Check if tag already exists
if git -C "$ROOT_DIR" tag -l "v$NEW_VERSION" | grep -q "v$NEW_VERSION"; then
    echo "Error: Tag v$NEW_VERSION already exists."
    echo "If you need to re-release, delete the tag first:"
    echo "  git tag -d v$NEW_VERSION"
    echo "  git push origin :refs/tags/v$NEW_VERSION"
    exit 1
fi

# Get current version
CURRENT_VERSION=$(grep 'set(PROJECT_VER' "$CMAKE_FILE" | sed 's/.*"\(.*\)".*/\1/')

echo "=== Releasing v$NEW_VERSION ==="

# Step 1: Update version in CMakeLists.txt
echo "[1/4] Updating version in CMakeLists.txt..."
if [ "$CURRENT_VERSION" = "$NEW_VERSION" ]; then
    echo "      Version already set to $NEW_VERSION"
else
    sed -i '' "s/set(PROJECT_VER \".*\")/set(PROJECT_VER \"$NEW_VERSION\")/" "$CMAKE_FILE"
    echo "      Version updated: $CURRENT_VERSION -> $NEW_VERSION"
fi

# Step 2: Commit (only if there are changes)
echo "[2/4] Committing..."
cd "$ROOT_DIR"
git add "$CMAKE_FILE"
if git diff --cached --quiet; then
    echo "      No changes to commit (version was already $NEW_VERSION)"
else
    git commit -m "Release v$NEW_VERSION"
    echo "      Committed"
fi

# Step 3: Tag
echo "[3/4] Creating tag v$NEW_VERSION..."
git tag -a "v$NEW_VERSION" -m "Release v$NEW_VERSION"
echo "      Tag created"

# Step 4: Push
echo "[4/4] Pushing to GitHub..."
git push && git push --tags

echo ""
echo "=== Done! ==="
echo ""
echo "GitHub Actions will now:"
echo "  - Build the firmware"
echo "  - Create the release at: https://github.com/muness/roon-knob/releases/tag/v$NEW_VERSION"
echo "  - Build and push the Docker image"
echo ""
echo "Monitor progress at: https://github.com/muness/roon-knob/actions"
