#!/bin/bash
# Sprint 0 / T4: boot the Haiku arm64 image under Apple's Virtualization.framework
# with VZ's own (2D) virtio-gpu, in a window, and record how far it gets.
#
# VZ gives Haiku no usable console, so evidence comes from two places:
#   - the window (watch it: EFI loader text? boot splash? desktop?), and
#   - Haiku's syslog, copied off the disk afterwards with scripts/haiku-syslog.sh.
# The image's old syslog is deleted first, and the result only counts if the
# syslog comes from a VZ boot (ACPI "oem id: APPLE"): an earlier run read a
# syslog left over from a QEMU boot of the same image and passed wrongly.
# Measured 2026-09-18 (RAM console, tools/hvgpu): with VZ's virtio-gpu and
# virtio-blk, Haiku never reaches userland -- virtio_block over PCI fails
# ("reading the partition table failed"), >1 vCPU hangs in SMP bring-up, and
# app_server hangs on VZ's virtio-gpu. tools/hvgpu (NVMe, 1 vCPU, our GPU) boots.
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
"$ROOT/scripts/haiku-syslog.sh" --clear "$W/haiku.img"

echo "T4: booting a copy of $(basename "$IMAGE") under VZ for ${RUN_SECONDS}s -- watch the window"
"$ROOT/build/bin/hvz" "$W/haiku.img" --efivars "$W/efivars" --seconds "$RUN_SECONDS" \
	--size 1280x800 --cpus 4 --memory 4 | tee "$W/hvz.log" || true

status=0
if "$ROOT/scripts/haiku-syslog.sh" "$W/haiku.img" "$W/syslog" > /dev/null; then
	result=$(python3 "$ROOT/sprint0/boot_markers.py" "$W/syslog/syslog") || status=1
	if ! grep -aq "oem id: APPLE" "$W/syslog/syslog"; then
		result="syslog is not from a VZ boot (no 'oem id: APPLE') -- stale log? $result"
		status=1
	fi
else
	result="no syslog written: Haiku did not reach userland (or never mounted its disk)"
	status=1
fi
echo "T4 $([ $status -eq 0 ] && echo PASS || echo FAIL): $result" | tee "$W/summary.txt"
exit $status
