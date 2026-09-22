#!/bin/sh
# Pack ProsePaint's sources for the recipe beside this script: prosepkg reads a
# file:// source from the recipe's own directory. The application is developed
# here, not fetched, so its tarball is made rather than downloaded. It is the
# committed state of prosepaint/app that is packed, never the working tree, and
# SOURCE_STATE inside says which commit it was.
#
#   packages/builder/overlay/haiku-apps/prosepaint/make-source.sh
#   scripts/prosepkg build prosepaint
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../../.." && pwd)
version=0.1
out="$here/prosepaint-$version.tar.gz"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# the committed state of prosepaint/app, never the working tree: a file
# someone is half-way through editing cannot go into a package that way
git -C "$root" archive --prefix="prosepaint-$version/" HEAD:prosepaint/app | tar xf - -C "$tmp"
# anything built that was committed: the recipe builds what it ships
(cd "$tmp/prosepaint-$version" && rm -f ProsePaint src/*.o)
cp "$here/ProsePaint.rdef" "$tmp/prosepaint-$version/"
echo "commit $(git -C "$root" rev-parse HEAD)" > "$tmp/prosepaint-$version/SOURCE_STATE"
tar czf "$out" -C "$tmp" "prosepaint-$version"
echo "$out"
