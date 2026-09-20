#!/bin/bash
# Boot prose-dev.image with a REAL QEMU window — for human driving.
#
# Firmware is pflash code + a PERSISTENT varstore (vm/vars.fd), so a
# resolution chosen in the firmware setup (Esc at the TianoCore splash →
# Device Manager → OVMF Platform Configuration → Set Resolution) sticks
# across boots. A USB keyboard is attached because the firmware menus
# don't read virtio input. The guest still gets its virtio keyboard and
# tablet for normal use.
#
# Usage: vm/run-windowed.sh
set -euo pipefail
VM="$(cd "$(dirname "$0")" && pwd)"
IMAGE="$VM/prose-dev.image"
FW_CODE="/opt/homebrew/share/qemu/edk2-aarch64-code.fd"
FW_VARS_TEMPLATE="/opt/homebrew/share/qemu/edk2-arm-vars.fd"
FW_VARS="$VM/vars.fd"

[ -f "$FW_CODE" ] || { echo "firmware missing: brew install qemu" >&2; exit 1; }
[ -f "$IMAGE" ] || { echo "no $IMAGE" >&2; exit 1; }
[ -f "$FW_VARS" ] || cp "$FW_VARS_TEMPLATE" "$FW_VARS"

mkdir -p "$VM/run"
: > "$VM/serial.log"
rm -f "$VM/monitor.sock" "$VM/qmp.sock" "$VM/qemu.pid"

exec qemu-system-aarch64 \
	-machine virt -cpu host -accel hvf \
	-smp 4 -m 3G \
	-drive "if=pflash,format=raw,file=$FW_CODE,readonly=on" \
	-drive "if=pflash,format=raw,file=$FW_VARS" \
	-drive "if=none,file=$IMAGE,format=raw,id=hd0" \
	-device virtio-blk-device,drive=hd0 \
	-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:9000-:9000" \
	-device virtio-net-device,netdev=net0 \
	-device ramfb \
	-device qemu-xhci \
	-device usb-kbd \
	-device virtio-keyboard-device -device virtio-tablet-device \
	-serial "file:$VM/serial.log" \
	-monitor "unix:$VM/monitor.sock,server,nowait" \
	-qmp "unix:$VM/qmp.sock,server,nowait" \
	-pidfile "$VM/qemu.pid" \
	"$@"
