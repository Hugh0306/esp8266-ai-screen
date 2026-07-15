#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CONFIGURATION="${1:-release}"
APP="$SCRIPT_DIR/.build/AIClockBridge.app"

cd "$SCRIPT_DIR"
swift build -c "$CONFIGURATION"
BIN_DIR="$(swift build -c "$CONFIGURATION" --show-bin-path)"

/bin/rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"
cp "$BIN_DIR/AIClockBridge" "$APP/Contents/MacOS/AIClockBridge"
cp "$SCRIPT_DIR/AIClockBridge-Info.plist" "$APP/Contents/Info.plist"
/usr/bin/ditto "$SCRIPT_DIR/Sources/AIClockBridge/Resources" "$APP/Contents/Resources"
cp "$SCRIPT_DIR/../docs/THIRD_PARTY_NOTICES.md" "$APP/Contents/Resources/THIRD_PARTY_NOTICES.md"
/usr/bin/codesign --force --deep --sign - "$APP"
/usr/bin/codesign --verify --deep --strict "$APP"

echo "$APP"
