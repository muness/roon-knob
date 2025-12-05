#!/bin/bash
set -euo pipefail

# Release firmware - bumps version, commits, tags, and pushes
# GitHub Actions builds the firmware and creates the release automatically
#
# Usage: ./scripts/release_firmware.sh <version>
# Example: ./scripts/release_firmware.sh 1.2.0

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
CMAKE_FILE="$ROOT_DIR/idf_app/CMakeLists.txt"

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

# Validate version format
if ! echo "$NEW_VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Error: Version must be semver format (e.g., 1.2.3)"
    exit 1
fi

# Check for uncommitted changes
if ! git -C "$ROOT_DIR" diff --quiet || ! git -C "$ROOT_DIR" diff --cached --quiet; then
    echo "Error: You have uncommitted changes. Please commit or stash them first."
    exit 1
fi

echo "=== Releasing v$NEW_VERSION ==="

# Step 1: Update version in CMakeLists.txt
echo "[1/4] Updating version in CMakeLists.txt..."
sed -i '' "s/set(PROJECT_VER \".*\")/set(PROJECT_VER \"$NEW_VERSION\")/" "$CMAKE_FILE"
echo "      Version set to $NEW_VERSION"

# Step 2: Commit
echo "[2/4] Committing..."
cd "$ROOT_DIR"
git add "$CMAKE_FILE"
git commit -m "Release v$NEW_VERSION"

# Step 3: Tag
echo "[3/4] Creating tag v$NEW_VERSION..."
git tag -a "v$NEW_VERSION" -m "Release v$NEW_VERSION"

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
