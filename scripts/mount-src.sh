#!/bin/sh
# Attach the case-sensitive build volume (after reboot or `hdiutil detach`).
# The sparse bundle lives in this repo; macOS mounts it at /Volumes/HaikuSrc.
set -e
IMAGE="$(cd "$(dirname "$0")/.." && pwd)/HaikuBuild.sparsebundle"

if [ -d /Volumes/HaikuSrc/haiku ]; then
	echo "Build volume already mounted at /Volumes/HaikuSrc"
	exit 0
fi

if command -v diskutil >/dev/null 2>&1 \
	&& diskutil image attach --help >/dev/null 2>&1; then
	diskutil image attach "$IMAGE"        # macOS 27+: hdiutil attach is deprecated
else
	hdiutil attach "$IMAGE"
fi
# Keep Spotlight off the build tree; it slows jam down noticeably.
mdutil -i off /Volumes/HaikuSrc >/dev/null 2>&1 || true
echo "Mounted at /Volumes/HaikuSrc"
