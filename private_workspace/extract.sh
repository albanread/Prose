#!/bin/bash
# Copy a file out of a run's image copy: extract.sh <name> <path-in-volume> [out-file]
#   e.g. extract.sh net /home/netprobe.txt
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:?usage: extract.sh <name> <path-in-volume> [out-file]}
GUEST=${2:?usage: extract.sh <name> <path-in-volume> [out-file]}
OUT=${3:-$PW_WORK/$NAME/$(basename "$GUEST")}
IMAGE="$PW_WORK/$NAME/haiku.img"
read -r START END < <(python3 - "$IMAGE" <<'PY'
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
printf '%s\n' "cp /myfs$GUEST :$OUT" "quit" \
	| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMAGE" > /dev/null 2>&1 || true
[ -f "$OUT" ] && cat "$OUT" || echo "no $GUEST in the image" >&2
