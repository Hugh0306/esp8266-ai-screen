#!/bin/zsh
set -e

SCRIPT_DIR="${0:A:h}"
APP="$SCRIPT_DIR/.build/AIClockBridge.app"

if pgrep -f "$APP/Contents/MacOS/AIClockBridge" >/dev/null 2>&1; then
  exit 0
fi

/usr/bin/open -n "$APP"
