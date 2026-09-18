#!/bin/bash
# The arm64 exception-to-signal mapping (patch 0029), end to end: builds
# trap_check.c for the guest with prosepkg's toolchain (read only), boots a
# copy of the image with it, runs it from a UserBootscript (trapprobe.sh),
# and prints its report and what debug_server logged about the crashes.
# usage: trap-test.sh [qemu|vz]   (default qemu)
#   PW_IMAGE picks the image (default: the private workspace build)
set -euo pipefail
source "$(dirname "$0")/env.sh"
HOST=${1:-qemu}
D="$PW_WORK/trap"
mkdir -p "$D"

PP=/Volumes/HaikuSrc/prose-packages
PROSE_SYSROOT=$PP/base/sysroot "$PP/env/bin/gcc" -O1 -Wall \
	-o "$D/trap_check" "$PW_ROOT/private_workspace/trap_check.c"

# an image with trap_check in /boot/home; run-*.sh copy it once more and add
# the UserBootscript
cp "$PW_IMAGE" "$D/base.img"
read -r START END < <(python3 - "$D/base.img" <<'PY'
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
printf '%s\n' "cp :$D/trap_check /myfs/home/trap_check" "sync" "quit" \
	| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$D/base.img" \
	> "$D/inject.log" 2>&1 || true

export PW_IMAGE="$D/base.img"
export PW_INJECT_SCRIPT="$PW_ROOT/private_workspace/trapprobe.sh"
case $HOST in
qemu)
	"$PW_ROOT/private_workspace/run-qemu.sh" --headless --copy --seconds 120
	NAME=qemu LOG="$PW_WORK/qemu/serial.log" ;;
vz)
	PW_SHARE= "$PW_ROOT/private_workspace/run-vz.sh" trap-vz --headless --seconds 120
	# debug_server's lines are userland output: not in the RAM console
	"$PW_ROOT/private_workspace/syslog.sh" trap-vz > /dev/null 2>&1 || true
	NAME=trap-vz LOG="$PW_WORK/trap-vz/syslog/syslog" ;;
*)
	echo "usage: trap-test.sh [qemu|vz]" >&2; exit 64 ;;
esac

echo ">>> debug_server and kernel lines:"
grep -aE 'debug_server: (Thread|Killing)|from user mode|unhandled|PANIC' "$LOG" \
	| awk '!seen[$0]++' | cut -c1-160 || true
echo ">>> the guest's report:"
"$PW_ROOT/private_workspace/extract.sh" "$NAME" /home/trap_check.txt "$D/trap_check.txt"
[ "$(tail -2 "$D/trap_check.txt" | head -1)" = PASS ]
