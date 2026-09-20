#!/bin/sh
# Pack ProseWriter's sources from this repository's working tree for the recipe
# beside this script: prosepkg reads a file:// source from the recipe's own
# directory. The application is developed here, not fetched, so its tarball is
# made rather than downloaded, and SOURCE_STATE inside it says which commit,
# and which uncommitted files, it was made from.
#
#   packages/builder/overlay/haiku-apps/prosewriter/make-source.sh
#   scripts/prosepkg build prosewriter
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../../.." && pwd)
version=0.1
out="$here/prosewriter-$version.tar.gz"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/prosewriter-$version"
(cd "$root/prosewriter/app" && tar cf - \
	--exclude='*.o' --exclude=sysroot --exclude=ProseWriter --exclude=proseagent \
	--exclude=pwquery --exclude=hello --exclude=probe --exclude=harness-launch \
	.) | (cd "$tmp/prosewriter-$version" && tar xf -)
cp "$here/ProseWriter.rdef" "$tmp/prosewriter-$version/"
{
	echo "commit $(git -C "$root" rev-parse HEAD)"
	git -C "$root" status --porcelain prosewriter/app | sed 's/^/dirty /'
} > "$tmp/prosewriter-$version/SOURCE_STATE"
tar czf "$out" -C "$tmp" "prosewriter-$version"
echo "$out"
