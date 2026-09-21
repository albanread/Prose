#!/bin/sh
# Pack ProseWriter's sources for the recipe beside this script: prosepkg reads a
# file:// source from the recipe's own directory. The application is developed
# here, not fetched, so its tarball is made rather than downloaded. It is the
# committed state of prosewriter/app that is packed, never the working tree, and
# SOURCE_STATE inside says which commit it was.
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
# the committed state of prosewriter/app, never the working tree: a file
# someone is half-way through editing cannot go into a package that way
git -C "$root" archive --prefix="prosewriter-$version/" HEAD:prosewriter/app | tar xf - -C "$tmp"
# anything built that was committed, and the developers' own tools: the
# recipe builds what it ships
(cd "$tmp/prosewriter-$version" && rm -rf sysroot ProseWriter proseagent pwquery \
	hello probe harness-launch src/*.o)
cp "$here/ProseWriter.rdef" "$tmp/prosewriter-$version/"
echo "commit $(git -C "$root" rev-parse HEAD)" > "$tmp/prosewriter-$version/SOURCE_STATE"
tar czf "$out" -C "$tmp" "prosewriter-$version"
echo "$out"
