#!/bin/sh
# Boot the Haiku ARM64 image in QEMU on Apple Silicon, using HVF
# (native virtualization -- no emulation).
#
# Usage: scripts/run-qemu.sh [image] [extra qemu args...]
# Requires: brew install qemu   (firmware ships with it)
#
# Device choices (validated on this machine):
#   display: -device ramfb -- edk2's firmware framebuffer (800x600 BGRx).
#           virtio-gpu-pci gets NO GOP from QEMU's edk2 (BltOnly, base 0),
#           so the Haiku loader would have nothing to draw on.
#   storage/net/input: virtio-mmio transport (-device virtio-*-device);
#           virtio-*-pci under HVF produced intermittent I/O errors.
set -e
IMAGE="${1:-/Volumes/HaikuSrc/haiku/haiku-mmc.image}"
[ $# -gt 0 ] && shift

FW="/opt/homebrew/share/qemu/edk2-aarch64-code.fd"
if [ ! -f "$FW" ]; then
	echo "UEFI firmware not found at $FW -- run: brew install qemu" >&2
	exit 1
fi
if [ ! -f "$IMAGE" ]; then
	echo "Image not found: $IMAGE -- run scripts/build-image.sh first" >&2
	exit 1
fi

exec qemu-system-aarch64 \
	-machine virt -cpu host -accel hvf \
	-smp 8 -m 4G \
	-bios "$FW" \
	-drive "if=none,file=$IMAGE,format=raw,id=hd0" \
	-device virtio-blk-device,drive=hd0 \
	-netdev user,id=net0 -device virtio-net-device,netdev=net0 \
	-device ramfb \
	-device virtio-keyboard-device -device virtio-tablet-device \
	-serial stdio \
	"$@"
