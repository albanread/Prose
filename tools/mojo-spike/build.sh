#!/bin/bash
# The M0 spike of docs/mojo.md: a Mojo program compiled on this Mac by the
# unchanged MojoCocoa compiler, linked by Haiku's cross toolchain, for Prose.
#
#   tools/mojo-spike/build.sh [hello|probe]   -> tools/mojo-spike/out/<name>
#
# The runtime is mojort_spike.c: the eight entry points hello needs and no
# more. probe.mojo does not build yet -- the stdlib refuses Haiku at
# get_errno -- and is here as the first target of M1.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MOJO="${MOJO_DIST:-/Applications/Roast/CocoaMojo/current}"
HAIKU="${HAIKU:-/Volumes/HaikuSrc/haiku}"
SYSROOT="${SYSROOT:-$(cd "$HERE/../../prosewriter/app/sysroot" && pwd)}"
X="$HAIKU/generated/cross-tools-arm64/bin/aarch64-unknown-haiku"
NAME="${1:-hello}"
OUT="$HERE/out"
mkdir -p "$OUT"

export MODULAR_MOJO_MAX_COCOAKB_PATH="$MOJO/share/cocoa.sqlite"
export MODULAR_MOJO_MAX_COMPILERRT_PATH="$MOJO/lib/libKGENCompilerRTShared.dylib"
export MODULAR_CRASH_REPORTING_ENABLED=false
export DYLD_LIBRARY_PATH="$MOJO/lib"
export MODULAR_CACHE_DIR="$OUT/cache"

"$MOJO/bin/cocoamojo-compiler" build -I "$MOJO/lib/mojo/stdlib" --emit object \
	--target-triple aarch64-unknown-haiku --target-cpu apple-m1 \
	"$HERE/$NAME.mojo" -o "$OUT/$NAME.o"
"$X-gcc" -O2 -Wall -Wextra -fPIC --sysroot "$SYSROOT" \
	-I"$SYSROOT/boot/system/develop/headers/posix" \
	-c "$HERE/mojort_spike.c" -o "$OUT/mojort_spike.o"
"$X-gcc" --sysroot "$SYSROOT" -L"$SYSROOT/boot/system/develop/lib" \
	-o "$OUT/$NAME" "$OUT/$NAME.o" "$OUT/mojort_spike.o"
echo "$OUT/$NAME"
