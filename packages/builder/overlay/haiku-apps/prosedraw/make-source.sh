#!/bin/sh
# Pack ProseDraw's sources for the recipe beside this script: prosepkg reads a
# file:// source from the recipe's own directory. It is the committed state of
# prosedraw/app that is packed, never the working tree, so a file someone is
# half-way through editing cannot go into a package; SOURCE_STATE inside says
# which commit it was.
#
#   packages/builder/overlay/haiku-apps/prosedraw/make-source.sh
#   scripts/prosepkg build prosedraw
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../../.." && pwd)
version=0.1
out="$here/prosedraw-$version.tar.gz"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git -C "$root" archive --prefix="prosedraw-$version/" HEAD:prosedraw/app | tar xf - -C "$tmp"
# anything built that was committed: the recipe builds its own
rm -f "$tmp/prosedraw-$version/ProseDraw" "$tmp/prosedraw-$version"/src/*.o
cp "$here/ProseDraw.rdef" "$tmp/prosedraw-$version/"
echo "commit $(git -C "$root" rev-parse HEAD)" > "$tmp/prosedraw-$version/SOURCE_STATE"
tar czf "$out" -C "$tmp" "prosedraw-$version"
echo "$out"
