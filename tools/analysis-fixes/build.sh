#!/bin/sh
# Cross-compile afxtest for Prose (arm64) on the Mac, with the Prose build
# tree's cross compiler and the sysroot ProseWriter uses. Tracker's own
# headers come from the Haiku tree (read only).
#
# Usage: build.sh [output directory]   (default: this directory)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE}
HAIKU=${HAIKU:-/Volumes/HaikuSrc/haiku}
SYSROOT=${SYSROOT:-$HERE/../../prosewriter/app/sysroot}
XGXX=$HAIKU/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++
# BColumnListView is a static library, linked into each program: link the
# tree's latest build of it (or, to test older code, COLUMNLISTVIEW=<archive>)
BUILT=$HAIKU/generated/objects/haiku/arm64/release
COLUMNLISTVIEW=${COLUMNLISTVIEW:-$BUILT/kits/interface/libcolumnlistview.a}
DEVELOP=$SYSROOT/boot/system/develop
H=$DEVELOP/headers

# the public kits, as ProseWriter has them; of the private headers only
# what the tests use, and the kernel's utilities (KMessage.h) last: their
# AutoLock.h would hide Tracker's, and private/kernel <string.h>
INCLUDES=""
for dir in $(find "$H/os" -type d -maxdepth 1) "$H/posix" "$H/bsd"; do
	INCLUDES="$INCLUDES -I$dir"
done
for dir in app interface media media/experimental shared storage support \
		tracker; do
	INCLUDES="$INCLUDES -I$H/private/$dir"
done
INCLUDES="$INCLUDES -I$HAIKU/src/kits/tracker -idirafter $H/private/kernel/util"
INCLUDES="$INCLUDES -idirafter $H/private -idirafter $H/private/kernel"

"$XGXX" -O2 -g -Wall -Wextra -Wpointer-arith -Wno-unused-parameter \
	-Wno-missing-field-initializers -Wno-multichar -std=c++17 \
	--sysroot "$SYSROOT" $INCLUDES \
	-o "$OUT/afxtest" "$HERE/afxtest.cpp" \
	"$COLUMNLISTVIEW" \
	-L"$DEVELOP/lib" -lbe -ltracker -ltranslation -lmedia -lbnetapi \
	-lnetwork -lstdc++ -lgcc_s

echo "built $OUT/afxtest"
