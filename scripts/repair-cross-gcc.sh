#!/bin/bash
# repair-cross-gcc.sh [--install] — rebuild the gcc/g++ drivers of a Haiku
# cross toolchain whose driver binaries were overwritten, without a full
# cross-tools rebuild (binutils, cc1/cc1plus, libgcc and libstdc++ stay).
#
# The configure line is read back from the toolchain's own configargs.h, so
# the drivers come out configured exactly like build_cross_tools_gcc4 made
# them (same prefix, same baked-in sysroot). The build runs in a scratch
# directory; nothing in the toolchain changes unless --install is given.
#
# Incident 2026-09-18: a package-builder wrapper symlinked into bin/ and a
# `cat >` through that symlink replaced aarch64-unknown-haiku-{gcc,g++} with
# a shell script that exec'd itself (hung every jam build).
#
#   TOOLS=<cross-tools dir>  (default: the main tree's cross-tools-arm64)
#   BUILDTOOLS=<buildtools>  (default: /Volumes/HaikuSrc/buildtools)
set -euo pipefail
TOOLS=${TOOLS:-/Volumes/HaikuSrc/haiku/generated/cross-tools-arm64}
BUILDTOOLS=${BUILDTOOLS:-/Volumes/HaikuSrc/buildtools}
MACHINE=aarch64-unknown-haiku
INSTALL=0
[ "${1:-}" = --install ] && INSTALL=1

ARGS_H=$(ls "$TOOLS"/lib/gcc/$MACHINE/*/plugin/include/configargs.h)
VERSION=$(basename "$(dirname "$(dirname "$(dirname "$ARGS_H")")")")
CONFIGURE_LINE=$(sed -n 's/^static const char configuration_arguments\[\] = "\(.*\)";$/\1/p' "$ARGS_H")
[ -n "$CONFIGURE_LINE" ] || { echo "cannot read configure line from $ARGS_H" >&2; exit 1; }
case "$CONFIGURE_LINE" in
"$BUILDTOOLS"/gcc/configure\ *) ;;
*) echo "toolchain was configured from another tree: $CONFIGURE_LINE" >&2; exit 1 ;;
esac
echo "gcc $VERSION: $CONFIGURE_LINE"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/repair-cross-gcc.XXXXXX")
# PATH exposes only the binutils (never the drivers being replaced: a broken
# driver that execs itself would hang configure).
mkdir "$WORK/bin" "$WORK/build"
for t in addr2line ar as c++filt elfedit gprof ld ld.bfd nm objcopy objdump \
		ranlib readelf size strings strip; do
	ln -s "$TOOLS/bin/$MACHINE-$t" "$WORK/bin/$MACHINE-$t"
done
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin:$WORK/bin"
export LC_ALL=POSIX
cd "$WORK/build"
# same flags as build_cross_tools_gcc4
eval "CFLAGS='-O2' CXXFLAGS='-O2 -std=c++14' $CONFIGURE_LINE" > configure.log 2>&1
# all-gcc stops at fixincludes, which needs the target headers in the
# sysroot (build_cross_tools_gcc4 copies them in and deletes them again);
# the installed include-fixed/ is untouched, only the drivers are needed.
make -j"$(sysctl -n hw.ncpu)" all-gcc > make.log 2>&1 || true
for d in xgcc xg++; do
	[ -x "gcc/$d" ] || { echo "gcc/$d was not built, see $WORK/build/make.log" >&2; exit 1; }
done
echo "drivers built in $WORK/build/gcc"

if [ $INSTALL = 1 ]; then
	B="$TOOLS/bin"
	# rename into place (atomic), then restore gcc's hard-link pairs
	cp gcc/xgcc "$B/.$MACHINE-gcc.new" && mv -f "$B/.$MACHINE-gcc.new" "$B/$MACHINE-gcc"
	ln -f "$B/$MACHINE-gcc" "$B/$MACHINE-gcc-$VERSION"
	cp gcc/xg++ "$B/.$MACHINE-g++.new" && mv -f "$B/.$MACHINE-g++.new" "$B/$MACHINE-g++"
	ln -f "$B/$MACHINE-g++" "$B/$MACHINE-c++"
	"$B/$MACHINE-gcc" -print-libgcc-file-name
	echo "installed into $B"
	rm -rf "$WORK"
else
	echo "dry run: rerun with --install to replace the drivers in $TOOLS/bin"
fi
