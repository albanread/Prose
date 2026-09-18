#!/bin/bash
# Sprint 0 / T2: boot the Haiku arm64 image under QEMU + HVF in a matrix of
# device configurations, headless, and record how far each boot gets.
# The reference row is the validated configuration in scripts/run-qemu.sh
# (virtio-mmio devices + ramfb). The other rows vary transport, disk interface
# and display to pin down BUILDING.md's findings (PCI I/O errors, display).
#
# Every run uses -snapshot, so the image is never modified. At the end of each
# run the ramfb and virtio-gpu consoles are captured with `screendump`.
#
# usage: t2-qemu-haiku-boot.sh [seconds-per-run]   (default 90)
# Needs the build volume mounted (scripts/mount-src.sh) and a built image.
# Output: work/t2/summary.txt. Exit 1 if the reference row fails.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${WORK:-$ROOT/work}/t2"
IMAGE=${HAIKU_IMAGE:-/Volumes/HaikuSrc/haiku/haiku-mmc.image}
FW=${EDK2_FW:-/opt/homebrew/share/qemu/edk2-aarch64-code.fd}
QEMU=${QEMU:-qemu-system-aarch64}
SECONDS_PER_RUN=${1:-90}

[ -f "$IMAGE" ] || { echo "T2: image not found: $IMAGE (scripts/mount-src.sh, scripts/build-image.sh)"; exit 1; }
rm -rf "$W"
mkdir -p "$W"

run() { # name transport(mmio|pci) disk(blk|scsi) displays(ramfb|both)
	local name=$1 transport=$2 disk=$3 displays=$4 sfx dir mon
	[ "$transport" = pci ] && sfx=pci || sfx=device
	dir="$W/$name"
	mon="$dir/monitor.sock"
	mkdir -p "$dir"
	local args=(-M virt -cpu host -accel hvf -smp 8 -m 4G -bios "$FW" -snapshot -no-reboot
		-drive "if=none,file=$IMAGE,format=raw,id=hd0"
		-netdev user,id=net0 -device "virtio-net-$sfx,netdev=net0"
		-device "virtio-keyboard-$sfx" -device "virtio-tablet-$sfx"
		-display none -serial "file:$dir/serial.log" -monitor "unix:$mon,server,nowait")
	if [ "$disk" = blk ]; then
		args+=(-device "virtio-blk-$sfx,drive=hd0")
	else
		args+=(-device "virtio-scsi-$sfx,id=scsi0" -device "scsi-hd,drive=hd0,bus=scsi0.0")
	fi
	args+=(-device ramfb,id=fb0)
	[ "$displays" = both ] && args+=(-device "virtio-gpu-$sfx,id=gpu0")

	echo "T2 $name: $transport/$disk/$displays for ${SECONDS_PER_RUN}s"
	"$QEMU" "${args[@]}" > "$dir/qemu.err" 2>&1 &
	local pid=$!
	sleep "$SECONDS_PER_RUN"
	local shots=("$dir/fb0.ppm")
	printf 'screendump %s -f ppm fb0\n' "$dir/fb0.ppm" | nc -U -w 2 "$mon" > /dev/null 2>&1 || true
	if [ "$displays" = both ]; then
		shots+=("$dir/gpu0.ppm")
		printf 'screendump %s -f ppm gpu0\n' "$dir/gpu0.ppm" | nc -U -w 2 "$mon" > /dev/null 2>&1 || true
	fi
	printf 'quit\n' | nc -U -w 2 "$mon" > /dev/null 2>&1 || true
	wait "$pid" 2>/dev/null || true
	for s in "${shots[@]}"; do
		[ -f "$s" ] && sips -s format png "$s" --out "${s%.ppm}.png" > /dev/null 2>&1 || true
	done
	local result rc=0
	result=$(python3 "$ROOT/sprint0/boot_markers.py" "$dir/serial.log" "${shots[@]}") || rc=$?
	printf '%-10s %s %s\n' "$name" "$([ $rc -eq 0 ] && echo PASS || echo FAIL)" "$result" | tee -a "$W/summary.txt"
	return $rc
}

: > "$W/summary.txt"
status=0
run reference mmio blk  ramfb || status=1   # = scripts/run-qemu.sh
run pci-blk   pci  blk  ramfb || true
run mmio-scsi mmio scsi ramfb || true
run pci-scsi  pci  scsi ramfb || true
run mmio-gpu  mmio blk  both  || true       # does Haiku's virtio_gpu driver take over?
echo
echo "T2 $([ $status -eq 0 ] && echo PASS || echo FAIL): reference row; see $W/summary.txt and $W/*/{serial.log,*.png}"
exit $status
