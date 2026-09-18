#!/bin/bash
# Sprint 0 / T4: boot the Haiku arm64 image under Apple's Virtualization.framework
# with VZ's own (2D) virtio-gpu, in a window, and record how far it gets.
#
# VZ gives Haiku no usable console, so evidence comes from two places:
#   - the window (watch it: EFI loader text? boot splash? desktop?), and
#   - Haiku's syslog, copied off the disk afterwards with scripts/haiku-syslog.sh.
# Measured on macOS 27 (150 s run, 2026-09-18): with hvz's serial port and
# virtio-gpu attached, the firmware publishes SPCR and a linear GOP, so the
# loader and framebuffer markers read True. The window stays black anyway:
# the GOP is not scan-out backed, so pixels written after ExitBootServices
# never reach VZ's display, and Haiku's virtio_gpu driver does not bind VZ's
# device (no EDID). Graphics come from the EFI framebuffer path, invisibly.
#
# usage: t4-vz-haiku-boot.sh [seconds]   (default 120)
# Needs the build volume mounted (scripts/mount-src.sh) and a built image.
# VZ writes to its disk, so the test boots a copy in work/t4, never the original.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${WORK:-$ROOT/work}/t4"
IMAGE=${HAIKU_IMAGE:-/Volumes/HaikuSrc/haiku/haiku-mmc.image}
RUN_SECONDS=${1:-120}

[ -f "$IMAGE" ] || { echo "T4: image not found: $IMAGE (scripts/mount-src.sh, scripts/build-image.sh)"; exit 1; }
"$ROOT/tools/build.sh"
rm -rf "$W"
mkdir -p "$W"
cp "$IMAGE" "$W/haiku.img"

echo "T4: booting a copy of $(basename "$IMAGE") under VZ for ${RUN_SECONDS}s -- watch the window"
"$ROOT/build/bin/hvz" "$W/haiku.img" --efivars "$W/efivars" --seconds "$RUN_SECONDS" \
	--size 1280x800 --cpus 4 --memory 4 | tee "$W/hvz.log" || true

status=0
if "$ROOT/scripts/haiku-syslog.sh" "$W/haiku.img" "$W/syslog" > /dev/null; then
	result=$(python3 "$ROOT/sprint0/boot_markers.py" "$W/syslog/syslog") || status=1
else
	result="no syslog written: Haiku did not reach userland (or never mounted its disk)"
	status=1
fi
echo "T4 $([ $status -eq 0 ] && echo PASS || echo FAIL): $result" | tee "$W/summary.txt"
exit $status
