#!/bin/bash
# boot-test.sh <probe> [<port|package|@set>...] — run a probe script on Prose.
#
# Installs the packages, if any (prosepkg install adds their requirements),
# into a clone of the image, runs <probe> from a UserBootscript with its
# output in /boot/home/probe.txt, powers off, and prints that file. Exit
# status 0 when the probe's last line is "PASS".
#   PW_IMAGE: the image to clone (default: the image scripts/build-image.sh built)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/private_workspace/env.sh"
PROBE=${1:?usage: boot-test.sh <probe> [<port|package|@set>...]}
shift
PP=/Volumes/HaikuSrc/prose-packages
NAME=$(basename "$PROBE" .sh)
WORK=$PP/images/test-$NAME
BFS_SHELL=$PP/hosttools/bin/bfs_shell
mkdir -p "$WORK"
IMG=$WORK/haiku.img

cp -c "$PW_IMAGE" "$IMG"
[ $# -eq 0 ] || "$ROOT/scripts/prosepkg" install "$IMG" "$@" | sed -n '1p'
{
	echo '#!/bin/sh'
	echo '/boot/home/probe.sh > /boot/home/probe.txt 2>&1'
	echo 'sync'
	echo 'shutdown -q'
} > "$WORK/UserBootscript"
read -r START END < <(python3 - "$IMG" <<'PY'
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
for i in range(4):
    e = mbr[446 + 16 * i:462 + 16 * i]
    if e[4] == 0xEB:
        lba, n = struct.unpack("<II", e[8:16])
        print(lba * 512, (lba + n) * 512)
PY
)
# fs_shell leaks a vnode (and cannot unmount) after a failing mkdir or a
# cp over an existing file: only issue commands that succeed
bfs() { printf '%s\n' "$@" quit | "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG"; }
BOOT=/myfs/home/config/settings/boot
listing=$(bfs "ls $BOOT" 2>&1 || true)
case "$listing" in
*"No such file"*) bfs "mkdir -p $BOOT" >/dev/null ;;
*UserBootscript*) bfs "rm $BOOT/UserBootscript" >/dev/null ;;
esac
cp "$PROBE" "$WORK/probe.sh"
chmod +x "$WORK/probe.sh"
bfs "cp :$WORK/UserBootscript $BOOT/UserBootscript" "cp :$WORK/probe.sh /myfs/home/probe.sh" \
	"sync" >/dev/null
# a regular image would otherwise wait in FirstBootPrompt for a click
"$ROOT/private_workspace/skip-first-boot-prompt.sh" "$IMG" > /dev/null

echo ">>> booting $IMG (headless QEMU, serial: $WORK/serial.log)"
gtimeout 300 qemu-system-aarch64 -M virt -cpu host -accel hvf -smp 4 -m 2G \
	-bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd -no-reboot \
	-drive "if=none,file=$IMG,format=raw,id=hd0" -device virtio-blk-pci,drive=hd0 \
	-device virtio-keyboard-pci -device virtio-tablet-pci -device ramfb \
	-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
	-serial "file:$WORK/serial.log" -display none || echo ">>> (qemu ended: $?)"

rm -f "$WORK/probe.txt"
bfs "cp /myfs/home/probe.txt :$WORK/probe.txt" >/dev/null 2>&1 || true
if [ -f "$WORK/probe.txt" ]; then
	cat "$WORK/probe.txt"
	[ "$(tail -1 "$WORK/probe.txt")" = PASS ]
else
	echo "no result: the guest did not write /boot/home/probe.txt" >&2
	exit 1
fi
