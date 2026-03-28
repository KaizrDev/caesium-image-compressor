#!/bin/sh
# Quick Action / Automator: Run Shell Script, pass input as arguments.
# Set APP to your installed bundle binary if not under /Applications.
APP="/Applications/Caesium Image Compressor.app/Contents/MacOS/Caesium Image Compressor"
if [ ! -x "$APP" ]; then
  echo "Caesium not found at $APP" >&2
  exit 1
fi
exec "$APP" --compress-context "$@"
