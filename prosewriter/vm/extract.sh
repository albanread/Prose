#!/bin/bash
# Copy a file out of prose-dev.image's BFS partition (VM must NOT be running).
# Usage: vm/extract.sh <guest-path> [out-file]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BFS_SHELL="/Volumes/HaikuSrc/haiku/generated/objects/darwin/arm64/release/tools/bfs_shell/bfs_shell"
IMG="$HERE/prose-dev.image"

GUEST=${1:?usage: extract.sh <guest-path> [out-file]}
case "$GUEST" in /boot/*) GUEST="${GUEST#/boot}";; esac  # fs_shell is volume-rooted
OUT=${2:-"$HERE/$(basename "$GUEST")"}

if [ -e "$HERE/qemu.pid" ] && kill -0 "$(cat "$HERE/qemu.pid")" 2>/dev/null; then
	echo "VM is running — shut it down first" >&2
	exit 1
fi

read -r START END < <(python3 - "$IMG" <<'PY'
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
for i in range(4):
    entry = mbr[446 + 16 * i:462 + 16 * i]
    if entry[4] == 0xEB:
        lba, count = struct.unpack("<II", entry[8:16])
        print(lba * 512, (lba + count) * 512)
        break
else:
    sys.exit("no BFS partition")
PY
)

rm -f "$OUT"
printf '%s\n' "cp /myfs$GUEST :$OUT" sync quit \
	| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG" > /dev/null 2>&1 || true
if [ -f "$OUT" ]; then
	echo "extracted $GUEST -> $OUT"
else
	echo "no $GUEST in the image" >&2
	exit 1
fi
