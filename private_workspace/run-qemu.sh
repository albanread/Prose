#!/bin/bash
# Boot the private image under QEMU + HVF. Modern virtio-pci devices by default
# (what Virtualization.framework uses too), ramfb for the boot display, -snapshot.
# usage: run-qemu.sh [--mmio] [--headless] [--seconds N] [-- extra qemu args]
set -euo pipefail
source "$(dirname "$0")/env.sh"
[ -f "$PW_IMAGE" ] || { echo "no image at $PW_IMAGE: run build.sh" >&2; exit 1; }
FW=/opt/homebrew/share/qemu/edk2-aarch64-code.fd
SFX=pci HEADLESS=0 SECONDS_LIMIT="" SOUND="" IMAGE="$PW_IMAGE" SNAPSHOT=-snapshot
EXTRA=()
while [ $# -gt 0 ]; do
	case $1 in
	--mmio) SFX=device ;;
	--headless) HEADLESS=1 ;;
	--seconds) SECONDS_LIMIT=$2; shift ;;
	--sound) SOUND=$2; shift ;;			# wav (recorded to work/qemu/out.wav) or coreaudio
	--copy) IMAGE="$PW_WORK/qemu/haiku.img"; SNAPSHOT="" ;;	# boot a fresh copy read-write (for injected scripts)
	--) shift; EXTRA=("$@"); break ;;
	*) echo "unknown option $1" >&2; exit 64 ;;
	esac
	shift
done
D="$PW_WORK/qemu"
mkdir -p "$D"
if [ -z "$SNAPSHOT" ]; then
	cp "$PW_IMAGE" "$IMAGE"
	if [ -n "${PW_INJECT_SCRIPT:-}" ]; then
		read -r START END < <(python3 - "$IMAGE" <<'PY'
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
for i in range(4):
    entry = mbr[446 + 16 * i:462 + 16 * i]
    if entry[4] == 0xEB:
        lba, count = struct.unpack("<II", entry[8:16])
        print(lba * 512, (lba + count) * 512)
        break
PY
)
		printf '%s\n' "mkdir /myfs/home/config/settings/boot" \
			"cp :$(cd "$(dirname "$PW_INJECT_SCRIPT")" && pwd)/$(basename "$PW_INJECT_SCRIPT") /myfs/home/config/settings/boot/UserBootscript" \
			"sync" "quit" \
			| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMAGE" > "$D/inject.log" 2>&1 || true
		echo ">>> injected $PW_INJECT_SCRIPT as UserBootscript"
	fi
fi
args=(-M virt -cpu host -accel hvf -smp 8 -m 4G -bios "$FW" $SNAPSHOT -no-reboot
	-drive "if=none,file=$IMAGE,format=raw,id=hd0" -device "virtio-blk-$SFX,drive=hd0"
	-netdev user,id=net0 -device "virtio-net-$SFX,netdev=net0"
	-device "virtio-keyboard-$SFX" -device "virtio-tablet-$SFX" -device ramfb
	-serial "file:$D/serial.log" -monitor "unix:$D/monitor.sock,server,nowait")
if [ "$HEADLESS" = 1 ]; then args+=(-display none); else args+=(-display cocoa); fi
case "$SOUND" in
	wav) args+=(-audiodev "wav,id=snd0,path=$D/out.wav,out.frequency=48000" -device "virtio-sound-$SFX,audiodev=snd0") ;;
	coreaudio) args+=(-audiodev coreaudio,id=snd0 -device "virtio-sound-$SFX,audiodev=snd0") ;;
esac
echo ">>> serial: $D/serial.log"
if [ -n "$SECONDS_LIMIT" ]; then
	# system_powerdown first so the guest syncs its disk (a hard kill loses written files)
	( sleep $((SECONDS_LIMIT - 30)); echo system_powerdown | nc -U "$D/monitor.sock" > /dev/null 2>&1 ) &
	gtimeout "$SECONDS_LIMIT" qemu-system-aarch64 "${args[@]}" ${EXTRA[@]+"${EXTRA[@]}"} || true
	python3 "$PW_ROOT/sprint0/boot_markers.py" "$D/serial.log" || true
else
	exec qemu-system-aarch64 "${args[@]}" ${EXTRA[@]+"${EXTRA[@]}"}
fi
