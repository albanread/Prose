#!/bin/bash
# Make a fresh image copy boot straight to the desktop.
#
# A regular image (the prose profile) shows FirstBootPrompt -- language and
# keymap -- and launch_daemon starts the desktop only after it is answered
# (data/launch/user: no "Locale settings" file means the first_boot target).
# Every run of run-vz.sh / run-qemu.sh / boot-test.sh boots a fresh copy, so
# each would ask again. An empty "Locale settings" file skips it; the locale
# falls back to its defaults. A copy that has the file already is left alone.
#
# usage: skip-first-boot-prompt.sh <image>
set -euo pipefail
source "$(dirname "$0")/env.sh"
IMG=${1:?usage: skip-first-boot-prompt.sh <image>}
read -r START END < <(python3 - "$IMG" <<'PY'
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
bfs() { printf '%s\n' "$@" quit | "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG"; }
# fs_shell cannot cp over an existing file (it leaks a vnode and cannot unmount)
case "$(bfs 'ls /myfs/home/config/settings' 2>&1)" in
*"Locale settings"*) exit 0 ;;
esac
empty=$(mktemp "${TMPDIR:-/tmp}/locale-settings.XXXXXX")
bfs "cp :$empty \"/myfs/home/config/settings/Locale settings\"" sync > /dev/null
rm -f "$empty"
echo ">>> first-boot prompt skipped (empty Locale settings; PW_FIRST_BOOT_PROMPT=1 keeps it)"
