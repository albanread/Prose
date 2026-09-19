#!/bin/bash
# Boot prose-dev.image (our own clone of the built Prose image) under
# QEMU/HVF, headless, with monitor + QMP sockets for automation.
#
# Usage: vm/run.sh [-k]   (-k: keep the fresh-copy semantics aside; boots as-is)
#
# Devices follow scripts/run-qemu.sh's validated matrix: ramfb display,
# virtio-mmio devices, user-mode networking. Added for automation:
#   -display none            headless; screenshots come from the QMP console
#   -monitor unix:…sock      HMP (screendump)
#   -qmp     unix:…qmp       QMP (input-send-event: absolute tablet, keys)
#   hostfwd 127.0.0.1:9000   the guest automation agent (proseagent)
set -euo pipefail
VM="$(cd "$(dirname "$0")" && pwd)"
IMAGE="$VM/prose-dev.image"
FW="/opt/homebrew/share/qemu/edk2-aarch64-code.fd"

[ -f "$FW" ] || { echo "UEFI firmware missing: brew install qemu" >&2; exit 1; }
[ -f "$IMAGE" ] || { echo "no $IMAGE (see README)" >&2; exit 1; }

mkdir -p "$VM/run"
: > "$VM/serial.log"
rm -f "$VM/monitor.sock" "$VM/qmp.sock" "$VM/qemu.pid"

exec qemu-system-aarch64 \
	-machine virt -cpu host -accel hvf \
	-smp 4 -m 3G \
	-bios "$FW" \
	-drive "if=none,file=$IMAGE,format=raw,id=hd0" \
	-device virtio-blk-device,drive=hd0 \
	-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:9000-:9000" \
	-device virtio-net-device,netdev=net0 \
	-device ramfb \
	-device virtio-keyboard-device -device virtio-tablet-device \
	-display none \
	-serial "file:$VM/serial.log" \
	-monitor "unix:$VM/monitor.sock,server,nowait" \
	-qmp "unix:$VM/qmp.sock,server,nowait" \
	-pidfile "$VM/qemu.pid" \
	"$@"
