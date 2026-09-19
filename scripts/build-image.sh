#!/bin/sh
# Build the Prose image (Haiku arm64, minimum profile -- see BUILDING.md §5)
# from the one Haiku tree, /Volumes/HaikuSrc/haiku:
#   1. main's changes to Haiku (patches/haiku/) are applied as commits on the
#      branch "prose" -- only what is missing (scripts/apply-patches.sh)
#   2. the packages the tree lists beyond upstream's (codecs, the translators'
#      libraries) are put in place from prosepkg (scripts/local-packages.sh)
#   3. jam
# Requires: build volume mounted (scripts/mount-src.sh), cross-tools configured.
#
# Usage: build-image.sh [jam target]   (default: @prose-mmc, the fork's profile:
#        the regular image plus Prose's packages and applications; @minimum-mmc
#        for the bare system)
set -e
TARGET="${1:-@prose-mmc}"
ROOT="/Volumes/HaikuSrc/haiku"

# jam lives in ~/bin; brew bison/gettext are keg-only and must precede system ones
export PATH="$HOME/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/gettext/bin:$PATH"
# jam opens a lot of files at once
ulimit -n 1024

"$(dirname "$0")/apply-patches.sh" "$ROOT"
"$(dirname "$0")/local-packages.sh" "$ROOT"

cd "$ROOT"
JOBS=$(sysctl -n hw.ncpu)
echo ">>> jam -q -j$JOBS $TARGET"
jam -q -j"$JOBS" "$TARGET"

echo
ls -lh "$ROOT/haiku-mmc.image"
echo "Run it with: scripts/run-qemu.sh"
