#!/bin/bash
# test-codecs.sh [image] — boot-test the codec packages on a Prose image.
#
# Installs codec_check (its requirements pull in every codec library) into
# a clone of the image, boots it headless under QEMU, runs codec_check from
# a UserBootscript, powers off, and prints what the guest wrote.
#   image: default the private workspace build (PW_IMAGE)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/private_workspace/env.sh"
SRC=${1:-$PW_IMAGE}
PP=/Volumes/HaikuSrc/prose-packages
WORK=$PP/images/codec-test
BFS_SHELL=$PP/hosttools/bin/bfs_shell
mkdir -p "$WORK"
IMG=$WORK/haiku.img

"$ROOT/scripts/prosepkg" build codec_check
cp -c "$SRC" "$IMG"
"$ROOT/scripts/prosepkg" install "$IMG" codec_check

cat > "$WORK/UserBootscript" <<'EOF'
#!/bin/sh
out=/boot/home/codec_check.txt
{
	echo "== $(uname -a)"
	echo
	codec_check
	echo "codec_check exit status: $?"
} > $out 2>&1
sync
shutdown -q
EOF
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
bfs "cp :$WORK/UserBootscript $BOOT/UserBootscript" "sync" >/dev/null

echo ">>> booting $IMG (headless QEMU, serial: $WORK/serial.log)"
gtimeout 300 qemu-system-aarch64 -M virt -cpu host -accel hvf -smp 4 -m 2G \
	-bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd -no-reboot \
	-drive "if=none,file=$IMG,format=raw,id=hd0" -device virtio-blk-pci,drive=hd0 \
	-device virtio-keyboard-pci -device virtio-tablet-pci -device ramfb \
	-serial "file:$WORK/serial.log" -display none || echo ">>> (qemu ended: $?)"

rm -f "$WORK/codec_check.txt"
printf '%s\n' "cp /myfs/home/codec_check.txt :$WORK/codec_check.txt" "quit" \
	| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG" >/dev/null 2>&1 || true
if [ -f "$WORK/codec_check.txt" ]; then
	cat "$WORK/codec_check.txt"
	grep -q "codec_check exit status: 0" "$WORK/codec_check.txt"
else
	echo "no result: the guest did not write /boot/home/codec_check.txt" >&2
	exit 1
fi
