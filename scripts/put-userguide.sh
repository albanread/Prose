#!/bin/bash
# Put the Prose User Guide into an image, as /boot/home/Desktop/Prose User Guide.pdf.
#
# The end-user build ships the guide with the machine it describes: it goes into
# the BFS partition of the image the installer carries, not into the application
# bundle, so it is there from the first boot whatever else is installed. Haiku
# sniffs the file's type from its content (attributes or not — the same way a
# file from a FAT volume opens), so a plain copy is enough.
#
# scripts/make-installer.sh runs this on the build's own copy of the machine,
# after copying it out of the Haiku tree and before anything is signed. The
# built tree is read except for the image named on the command line.
#
# usage: put-userguide.sh <image>
#   PROSE_USERGUIDE  path to the PDF (default docs/userguide/Prose-User-Guide.pdf)
#   BFS_SHELL        path to bfs_shell (default: the Haiku tree's host build)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG=${1:?usage: put-userguide.sh <image>}
GUIDE="${PROSE_USERGUIDE:-$ROOT/docs/userguide/Prose-User-Guide.pdf}"
BFS_SHELL="${BFS_SHELL:-/Volumes/HaikuSrc/haiku/generated/objects/darwin/arm64/release/tools/bfs_shell/bfs_shell}"

[ -f "$IMG" ] || { echo "!! no image at $IMG" >&2; exit 1; }
[ -f "$GUIDE" ] || { echo "!! no user guide at $GUIDE" >&2
	echo "   (docs/userguide/ holds its sources; DM exports it as a PDF, or set PROSE_USERGUIDE)" >&2
	exit 1; }
[ -x "$BFS_SHELL" ] || { echo "!! no bfs_shell at $BFS_SHELL (build the Haiku tree's host tools, or set BFS_SHELL)" >&2; exit 1; }

# the BFS partition, found the way every script here finds it: the MBR entry
# whose type is 0xEB, never an assumed offset (docs/storage.md)
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
[ -n "${START:-}" ] || { echo "!! $IMG has no BFS (0xEB) partition" >&2; exit 1; }

bfs() { printf '%s\n' "$@" quit | "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG"; }
DEST="/myfs/home/Desktop/Prose User Guide.pdf"

# fs_shell cannot cp over an existing file (it leaks a vnode and cannot
# unmount — skip-first-boot-prompt.sh's discovery), so an old copy goes first.
# mkdir of an existing directory is the same story, hence the ls checks.
if ! bfs 'ls /myfs/home/Desktop' >/dev/null 2>&1; then
	bfs 'mkdir /myfs/home/Desktop'
fi
if bfs 'ls /myfs/home/Desktop' 2>&1 | grep -q 'Prose User Guide'; then
	bfs "rm \"$DEST\""
fi
# the host path is colon-prefixed and unquoted (fs_shell's parser, and the
# guide's path has no spaces); the destination is quoted because its name does
bfs "cp :$GUIDE \"$DEST\"" sync

# fs_shell exits 0 even when a command failed, so believe the file, not the
# status: the copy counts only once the volume lists it
if ! bfs 'ls /myfs/home/Desktop' 2>&1 | grep -q 'Prose User Guide'; then
	echo "!! the guide did not land in the machine" >&2
	exit 1
fi
SIZE=$(stat -f%z "$GUIDE")
echo ">>> user guide in the machine: home/Desktop/Prose User Guide.pdf ($SIZE bytes)"
