#!/bin/bash
# Build the image. There is one way to do that now: scripts/build-image.sh
# builds /Volumes/HaikuSrc/haiku on the branch "prose" (main's patches applied,
# the fork's packages from prosepkg, then jam). This just runs it.
# usage: build.sh [jam target]   (default @minimum-mmc)
exec "$(dirname "$0")/../scripts/build-image.sh" "$@"
