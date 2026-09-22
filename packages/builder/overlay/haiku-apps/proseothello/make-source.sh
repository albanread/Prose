#!/bin/sh
# Pack ProseOthello's sources for the recipe beside this script: prosepkg reads a
# file:// source from the recipe's own directory. The application is developed
# here, not fetched, so its tarball is made rather than downloaded. It is the
# committed state of proseothello/app that is packed, never the working tree, and
# SOURCE_STATE inside says which commit it was.
#
#   packages/builder/overlay/haiku-apps/proseothello/make-source.sh
#   scripts/prosepkg build proseothello
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../../.." && pwd)
version=0.1
out="$here/proseothello-$version.tar.gz"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# the committed state of proseothello/app, never the working tree: a file
# someone is half-way through editing cannot go into a package that way
git -C "$root" archive --prefix="proseothello-$version/" HEAD:proseothello/app | tar xf - -C "$tmp"
# anything built that was committed: the recipe builds what it ships
(cd "$tmp/proseothello-$version" && rm -f ProseOthello src/*.o)
cp "$here/ProseOthello.rdef" "$tmp/proseothello-$version/"
echo "commit $(git -C "$root" rev-parse HEAD)" > "$tmp/proseothello-$version/SOURCE_STATE"
tar czf "$out" -C "$tmp" "proseothello-$version"
echo "$out"
