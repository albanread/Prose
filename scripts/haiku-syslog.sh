#!/bin/bash
# Copy Haiku's syslog out of a Haiku disk image (MBR, BFS partition type 0xEB)
# with the host bfs_shell built by the Haiku build. Useful after booting under
# Virtualization.framework, which gives Haiku no serial port.
# bfs_shell mounts the volume read-write, so only use it on copies of images.
#
# usage: haiku-syslog.sh <image> <out-dir>     (needs scripts/mount-src.sh)
set -euo pipefail

IMAGE=${1:?usage: haiku-syslog.sh <image> <out-dir>}
OUT=${2:?usage: haiku-syslog.sh <image> <out-dir>}
BFS_SHELL=${BFS_SHELL:-/Volumes/HaikuSrc/haiku/generated/objects/darwin/arm64/release/tools/bfs_shell/bfs_shell}
[ -x "$BFS_SHELL" ] || {
	echo "bfs_shell not found at $BFS_SHELL -- mount the build volume (scripts/mount-src.sh)" >&2
	exit 1
}

read -r START END < <(python3 - "$IMAGE" <<'EOF'
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
for i in range(4):
    entry = mbr[446 + 16 * i:462 + 16 * i]
    if entry[4] == 0xEB:
        lba, count = struct.unpack("<II", entry[8:16])
        print(lba * 512, (lba + count) * 512)
        break
else:
    sys.exit("no BFS (type 0xEB) partition in the MBR")
EOF
)

mkdir -p "$OUT"
OUT_ABS=$(cd "$OUT" && pwd)
rm -f "$OUT_ABS/syslog" "$OUT_ABS/syslog.old"
printf '%s\n' "ls /myfs/system/var/log" \
	"cp /myfs/system/var/log/syslog :$OUT_ABS/syslog" \
	"cp /myfs/system/var/log/syslog.old :$OUT_ABS/syslog.old" \
	"quit" \
	| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMAGE" > "$OUT_ABS/bfs_shell.log" 2>&1 || true

if [ -s "$OUT_ABS/syslog" ]; then
	echo "$OUT_ABS/syslog"
else
	echo "no syslog found in the image (see $OUT_ABS/bfs_shell.log)" >&2
	exit 1
fi
