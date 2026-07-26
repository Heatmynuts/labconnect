#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=${0:A:h}
BUILD_DIR="$SCRIPT_DIR/build"
APP_DIR="$BUILD_DIR/LabConnect Key.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"

mkdir -p "$MACOS_DIR"

xcrun swiftc \
  "$SCRIPT_DIR/Sources/main.swift" \
  -o "$MACOS_DIR/LabConnectKey" \
  -framework AppKit \
  -framework ApplicationServices \
  -framework CoreBluetooth \
  -parse-as-library

cp "$SCRIPT_DIR/Info.plist" "$CONTENTS_DIR/Info.plist"
xattr -cr "$APP_DIR"
xattr -d com.apple.FinderInfo "$APP_DIR" 2>/dev/null || true
codesign --force --deep --sign - "$APP_DIR"

echo "$APP_DIR"
