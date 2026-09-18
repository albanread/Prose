#!/bin/sh
# Build the Haiku ARM64 MMC image.
# Requires: build volume mounted (scripts/mount-src.sh), cross-tools configured.
set -e
ROOT="/Volumes/HaikuSrc/haiku"

# jam lives in ~/bin; brew bison/gettext are keg-only and must precede system ones
export PATH="$HOME/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/gettext/bin:$PATH"
# jam opens a lot of files at once
ulimit -n 1024

cd "$ROOT"
JOBS=$(sysctl -n hw.ncpu)
echo ">>> jam -q -j$JOBS haiku-mmc.image"
jam -q -j"$JOBS" haiku-mmc.image

echo
echo "Image ready: $ROOT/generated/haiku-mmc.image"
echo "Run it with: scripts/run-qemu.sh"
