#!/bin/sh
# Pack ProseJulia's sources for the recipe beside this script: prosepkg reads a
# file:// source from the recipe's own directory. It is the committed state of
# prosejulia/app that is packed, never the working tree, so a file someone is
# half-way through editing cannot go into a package; SOURCE_STATE inside says
# which commit it was.
#
#   packages/builder/overlay/haiku-apps/prosejulia/make-source.sh
#   scripts/prosepkg build prosejulia
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../../.." && pwd)
version=0.1
out="$here/prosejulia-$version.tar.gz"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git -C "$root" archive --prefix="prosejulia-$version/" HEAD:prosejulia/app | tar xf - -C "$tmp"
# anything built that was committed: the recipe builds its own
rm -f "$tmp/prosejulia-$version/ProseJulia" "$tmp/prosejulia-$version"/src/*.o
cp "$here/ProseJulia.rdef" "$tmp/prosejulia-$version/"
echo "commit $(git -C "$root" rev-parse HEAD)" > "$tmp/prosejulia-$version/SOURCE_STATE"
tar czf "$out" -C "$tmp" "prosejulia-$version"
echo "$out"
