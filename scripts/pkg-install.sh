#!/bin/bash
# pkg-install.sh <name> [image] — install a built app into a Prose disk image
# with bfs_shell (host-side; image must not be in use by a VM), and launch it
# from UserBootscript so the next boot shows it on the desktop.
set -euo pipefail
NAME=${1:?usage: pkg-install.sh <name> [image]}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=${2:-$ROOT/work/pkgtest/disk.img}
BIN="$ROOT/packages/out/$NAME/$NAME"
BFS_SHELL=${BFS_SHELL:-/Volumes/HaikuSrc/haiku/generated/objects/darwin/arm64/release/tools/bfs_shell/bfs_shell}

[ -x "$BIN" ] || { echo "no built binary: $BIN (run scripts/pkg-build.sh $NAME)" >&2; exit 1; }
[ -f "$IMAGE" ] || { echo "no image: $IMAGE" >&2; exit 1; }

read -r START END < <(python3 - "$IMAGE" <<'EOF'
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
for i in range(4):
    e = mbr[446 + 16*i:462 + 16*i]
    if e[4] == 0xEB:
        lba, n = struct.unpack("<II", e[8:16])
        print(lba * 512, (lba + n) * 512)
        break
else:
    sys.exit("no BFS partition")
EOF
)

WORK=$(mktemp -d)
{
	printf 'mkdir myfs/system/non-packaged/apps\n'
	printf 'mkdir myfs/system/settings/boot\n'
	printf 'cp :%s myfs/system/non-packaged/apps/%s\n' "$BIN" "$NAME"
} > "$WORK/cmds"
# append the launch line to UserBootscript (create if absent)
cat > "$WORK/UserBootscript" <<EOF
/boot/system/apps/$NAME &
EOF
printf 'cp :%s myfs/system/settings/boot/UserBootscript\n' "$WORK/UserBootscript" >> "$WORK/cmds"
printf 'ls myfs/system/non-packaged/apps\nquit\n' >> "$WORK/cmds"

"$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMAGE" < "$WORK/cmds" \
	> "$WORK/bfs.log" 2>&1 || { cat "$WORK/bfs.log"; exit 1; }
tail -4 "$WORK/bfs.log"
echo "INSTALLED: $NAME -> $IMAGE (/boot/system/apps via non-packaged; launches at boot)"
rm -rf "$WORK"
