#!/bin/bash
# Install files from the host into prose-dev.image's BFS partition.
# The VM must NOT be running (fs_shell needs exclusive access).
#
# Usage: vm/install.sh <host-file> <guest-path>   e.g.
#       vm/install.sh app/ProseWriter /boot/home/apps/ProseWriter
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BFS_SHELL="/Volumes/HaikuSrc/haiku/generated/objects/darwin/arm64/release/tools/bfs_shell/bfs_shell"
IMG="$HERE/prose-dev.image"

SRC=${1:?usage: install.sh <host-file> <guest-path>}
DST=${2:?usage: install.sh <host-file> <guest-path>}
case "$DST" in /boot/*) DST="${DST#/boot}";; esac   # fs_shell is volume-rooted
SRC="$(cd "$(dirname "$SRC")" && pwd)/$(basename "$SRC")"

if [ -e "$HERE/qemu.pid" ] && kill -0 "$(cat "$HERE/qemu.pid")" 2>/dev/null; then
	echo "VM is running — shut it down first (vm/qmp.py hmp 'quit' or kill $(cat "$HERE/qemu.pid"))" >&2
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

# fs_shell can't cp over an existing file (leaks a vnode): rm first; mkdir
# may also fail if the directory exists, so it runs in its own invocation.
printf 'mkdir "/myfs%s"\nquit\n' "$(dirname "$DST")" \
	| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG" > /dev/null 2>&1 || true
printf '%s\n' \
	"rm \"/myfs$DST\"" \
	"cp :$SRC \"/myfs$DST\"" \
	sync quit \
	| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG" > /dev/null
echo "installed $SRC -> $DST"
