#!/bin/bash
# Boot the private image under QEMU + HVF. Modern virtio-pci devices by default
# (what Virtualization.framework uses too), ramfb for the boot display, -snapshot.
# usage: run-qemu.sh [--mmio] [--headless] [--seconds N] [-- extra qemu args]
set -euo pipefail
source "$(dirname "$0")/env.sh"
[ -f "$PW_IMAGE" ] || { echo "no image at $PW_IMAGE: run build.sh" >&2; exit 1; }
FW=/opt/homebrew/share/qemu/edk2-aarch64-code.fd
SFX=pci HEADLESS=0 SECONDS_LIMIT=""
EXTRA=()
while [ $# -gt 0 ]; do
	case $1 in
	--mmio) SFX=device ;;
	--headless) HEADLESS=1 ;;
	--seconds) SECONDS_LIMIT=$2; shift ;;
	--) shift; EXTRA=("$@"); break ;;
	*) echo "unknown option $1" >&2; exit 64 ;;
	esac
	shift
done
D="$PW_WORK/qemu"
mkdir -p "$D"
args=(-M virt -cpu host -accel hvf -smp 8 -m 4G -bios "$FW" -snapshot -no-reboot
	-drive "if=none,file=$PW_IMAGE,format=raw,id=hd0" -device "virtio-blk-$SFX,drive=hd0"
	-netdev user,id=net0 -device "virtio-net-$SFX,netdev=net0"
	-device "virtio-keyboard-$SFX" -device "virtio-tablet-$SFX" -device ramfb
	-serial "file:$D/serial.log" -monitor "unix:$D/monitor.sock,server,nowait")
if [ "$HEADLESS" = 1 ]; then args+=(-display none); else args+=(-display cocoa); fi
echo ">>> serial: $D/serial.log"
if [ -n "$SECONDS_LIMIT" ]; then
	gtimeout "$SECONDS_LIMIT" qemu-system-aarch64 "${args[@]}" ${EXTRA[@]+"${EXTRA[@]}"} || true
	python3 "$PW_ROOT/sprint0/boot_markers.py" "$D/serial.log" || true
else
	exec qemu-system-aarch64 "${args[@]}" ${EXTRA[@]+"${EXTRA[@]}"}
fi
