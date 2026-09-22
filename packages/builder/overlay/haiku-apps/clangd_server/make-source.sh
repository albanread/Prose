#!/bin/sh
# Pack clangd_server's sources for the recipe beside this script: prosepkg reads a
# file:// source from the recipe's own directory. It is the committed state of
# clangdserver/app that is packed, never the working tree, so a file someone is
# half-way through editing cannot go into a package; SOURCE_STATE inside says
# which commit it was.
#
#   packages/builder/overlay/haiku-apps/clangd_server/make-source.sh
#   scripts/prosepkg build clangd_server
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../../.." && pwd)
version=0.1
out="$here/clangd_server-$version.tar.gz"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git -C "$root" archive --prefix="clangd_server-$version/" HEAD:clangdserver/app | tar xf - -C "$tmp"
# anything built that was committed: the recipe builds its own
rm -f "$tmp/clangd_server-$version/clangd_server" "$tmp/clangd_server-$version/clangd_client" "$tmp/clangd_server-$version"/src/*.o
echo "commit $(git -C "$root" rev-parse HEAD)" > "$tmp/clangd_server-$version/SOURCE_STATE"
tar czf "$out" -C "$tmp" "clangd_server-$version"
echo "$out"
