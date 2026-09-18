#!/bin/sh
# Generate packages/incflags.txt: the cross-compiler include recipe for
# building Haiku-native software against the Prose fork.
#
# Layering matters for gcc's #include_next wrappers:
#   -isystem    : the Be/Haiku API headers (fork tree first, then sysroot)
#   -idirafter  : the libc / libstdc++ headers, which must come AFTER gcc's
#                 internal directories for include_next to terminate
set -e
HAIKU=/Volumes/HaikuSrc/haiku
SYSROOT=/Volumes/HaikuSrc/prose-packages/sysroot-arm64
OUT="$(cd "$(dirname "$0")" && pwd)/incflags.txt"

SYS=""
add_sys() { if [ -d "$1" ]; then SYS="$SYS -isystem $1"; fi; }
add_after() { if [ -d "$1" ]; then SYS="$SYS -idirafter $1"; fi; }

# The fork's tree is the source of truth (carries our patches); the sysroot
# fills gaps (packaged subsets are sometimes wider than the tree's exports).
for base in "$HAIKU/headers" "$SYSROOT/develop/headers"; do
	for d in "$base" "$base"/be "$base"/be/* "$base"/os/* "$base"/os/*/* \
	         "$base"/GL "$base"/alm "$base"/linprog "$base"/private/* \
	         "$base"/gnu "$base"/bsd "$base"/config "$base"/glibc; do
		add_sys "$d"
	done
done

# libc / C++ headers last, after gcc's own: include_next termination.
# C++ wrappers first (gcc's own order: C++ before libc), then the libc dirs.
for base in "$SYSROOT/develop/headers"; do
	for d in "$base"/c++ "$base"/c++/* "$base"/posix "$base"/posix/* \
	         "$base"/bsd/* "$base"/gnu/*; do
		add_after "$d"
	done
done

printf '%s\n' "$SYS" > "$OUT"
wc -c "$OUT"
