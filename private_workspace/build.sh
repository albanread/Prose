#!/bin/bash
# Build the private worktree's image (default target @minimum-mmc).
# usage: build.sh [jam target]
set -euo pipefail
source "$(dirname "$0")/env.sh"
[ -d "$PW_HAIKU/generated/build" ] || { echo "worktree not configured: $PW_HAIKU (see README.md)" >&2; exit 1; }
ulimit -n 1024
cd "$PW_HAIKU"
TARGET=${1:-@minimum-mmc}
echo ">>> $(git rev-parse --abbrev-ref HEAD) @ $(git rev-parse --short HEAD): jam -q -j$(sysctl -n hw.ncpu) $TARGET"
jam -q -j"$(sysctl -n hw.ncpu)" "$TARGET"
ls -lh "$PW_IMAGE"
