#!/bin/bash
set -euo pipefail

# Release firmware for OTA updates
# Usage: ./scripts/release_firmware.sh <version>
# Example: ./scripts/release_firmware.sh 1.1.0

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
IDF_APP_DIR="$ROOT_DIR/idf_app"
FIRMWARE_DIR="$ROOT_DIR/roon-extension/firmware"
CMAKE_FILE="$IDF_APP_DIR/CMakeLists.txt"
VERSION_JSON="$FIRMWARE_DIR/version.json"
RELEASE_NOTES_FILE="$ROOT_DIR/.release_notes.tmp"

if [ $# -lt 1 ]; then
    # Show current version and prompt
    CURRENT=$(grep 'set(PROJECT_VER' "$CMAKE_FILE" | sed 's/.*"\(.*\)".*/\1/')
    echo "Current version: $CURRENT"
    echo "Usage: $0 <new_version>"
    echo "Example: $0 1.1.0"
    exit 1
fi

NEW_VERSION="$1"

# Validate version format
if ! echo "$NEW_VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Error: Version must be semver format (e.g., 1.2.3)"
    exit 1
fi

echo "=== Firmware Release: v$NEW_VERSION ==="

# Step 0: Collect release notes
echo ""
echo "[0/6] Enter release notes (end with Ctrl-D on empty line):"
echo "---"
cat > "$RELEASE_NOTES_FILE"
echo "---"

# Validate release notes aren't empty
if [ ! -s "$RELEASE_NOTES_FILE" ]; then
    echo "Error: Release notes cannot be empty"
    rm -f "$RELEASE_NOTES_FILE"
    exit 1
fi

# Show what was entered
echo ""
echo "Release notes:"
cat "$RELEASE_NOTES_FILE"
echo ""
read -p "Continue with release? [y/N] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Release cancelled"
    rm -f "$RELEASE_NOTES_FILE"
    exit 1
fi

# Step 1: Update version in CMakeLists.txt
echo "[1/6] Updating version in CMakeLists.txt..."
sed -i '' "s/set(PROJECT_VER \".*\")/set(PROJECT_VER \"$NEW_VERSION\")/" "$CMAKE_FILE"
echo "      Version set to $NEW_VERSION"

# Step 2: Build firmware
echo "[2/6] Building firmware..."
cd "$IDF_APP_DIR"

# Source ESP-IDF environment and build
if [ -f ~/esp/esp-idf/export.sh ]; then
    source ~/esp/esp-idf/export.sh >/dev/null 2>&1
else
    echo "Error: ESP-IDF not found at ~/esp/esp-idf"
    rm -f "$RELEASE_NOTES_FILE"
    exit 1
fi

idf.py build

# Step 3: Copy binary
echo "[3/6] Copying binary to firmware directory..."
mkdir -p "$FIRMWARE_DIR"
cp "$IDF_APP_DIR/build/roon_knob.bin" "$FIRMWARE_DIR/roon_knob.bin"
BINARY_SIZE=$(stat -f%z "$FIRMWARE_DIR/roon_knob.bin" 2>/dev/null || stat -c%s "$FIRMWARE_DIR/roon_knob.bin")
echo "      Copied roon_knob.bin ($BINARY_SIZE bytes)"

# Step 4: Update version.json
echo "[4/6] Updating version.json..."
cat > "$VERSION_JSON" << EOF
{
  "version": "$NEW_VERSION",
  "file": "roon_knob.bin"
}
EOF
echo "      version.json updated"

# Step 5: Commit and tag
echo "[5/6] Committing and tagging..."
cd "$ROOT_DIR"
git add -A
git commit -m "Release firmware v$NEW_VERSION"
git tag -a "v$NEW_VERSION" -m "Release v$NEW_VERSION"
echo "      Created tag v$NEW_VERSION"

# Step 6: Create GitHub release
echo "[6/6] Creating GitHub release..."
RELEASE_NOTES=$(cat "$RELEASE_NOTES_FILE")
rm -f "$RELEASE_NOTES_FILE"

gh release create "v$NEW_VERSION" \
    --title "v$NEW_VERSION" \
    --notes "$RELEASE_NOTES" \
    "$FIRMWARE_DIR/roon_knob.bin"

echo ""
echo "=== Release v$NEW_VERSION complete ==="
echo ""
echo "GitHub release created with firmware binary attached."
echo ""
echo "Next steps:"
echo "  1. git push && git push --tags"
echo "  2. Docker image will be built automatically by GitHub Actions"
echo "     (or manually: cd roon-extension && docker build -t muness/roon-extension-knob:latest . && docker push)"
