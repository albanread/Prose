#!/bin/bash
# Assemble a private cross-link sysroot for ProseWriter (symlinks only).
#
# The cross-gcc looks for <sysroot>/boot/system/develop/{headers,lib} and
# <sysroot>/boot/system/lib. We point those at the Prose build's extracted
# packages — used strictly read-only.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
GEN=/Volumes/HaikuSrc/haiku/generated
DEVEL="$GEN/objects/haiku/arm64/packaging/packages_build/regular/hpkg_-haiku_devel.hpkg/contents"
RUNTIME="$GEN/objects/haiku/arm64/packaging/packages_build/regular/hpkg_-haiku.hpkg/contents"
GCCSYS="$GEN/build_packages/gcc_syslibs-13.3.0_2026_03_29_bootstrap-1-arm64"
GCCDEV="$GEN/build_packages/gcc_syslibs_devel-13.3.0_2026_03_29_bootstrap-1-arm64"

SR="$APP/sysroot"
rm -rf "$SR"
SYS="$SR/boot/system"
mkdir -p "$SYS/develop" "$SYS/lib"

# Link every file; symlinks keep their RELATIVE target so that e.g.
# develop/lib/libbe.so -> ../../lib/libbe.so resolves inside this sysroot
# (in the packages alone it dangles: the runtime libs live in haiku.hpkg).
ln -s "$DEVEL/develop/headers" "$SYS/develop/headers"
link_one() {
	local src=$1 dst=$2 b
	b=$(basename "$src")
	[ -e "$dst" ] && return 0
	if [ -L "$src" ]; then
		ln -s "$(readlink "$src")" "$dst"
	else
		ln -s "$src" "$dst"
	fi
}
mkdir -p "$SYS/develop/lib"
for src in "$DEVEL/develop/lib"/* "$GCCDEV/develop/lib"/*; do
	link_one "$src" "$SYS/develop/lib/$(basename "$src")"
done
for f in "$RUNTIME/lib"/* "$GCCSYS/lib"/*; do
	[ -e "$f" ] || continue
	link_one "$f" "$SYS/lib/$(basename "$f")"
done
echo "sysroot ready: $SR ($(ls "$SYS/develop/lib" | wc -l | tr -d ' ') devel libs, $(ls "$SYS/lib" | wc -l | tr -d ' ') runtime libs)"
