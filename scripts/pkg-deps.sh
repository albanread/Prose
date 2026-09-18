#!/bin/bash
# pkg-deps.sh — install third-party build dependencies into the sysroot,
# once, so every -l and #include resolves without special flags.
# Currently: freetype2 (from our own build's bootstrap packages).
set -euo pipefail
PROSE=/Volumes/HaikuSrc/prose-packages
SYS=$PROSE/sysroot-arm64
DL=/Volumes/HaikuSrc/haiku/generated/download
PKGTOOLS=/Volumes/HaikuSrc/haiku/generated/objects/darwin/arm64/release/tools/package/package

mkdir -p "$PROSE/deps/freetype"
cd "$PROSE/deps/freetype"
FT=$(ls "$DL"/freetype_devel-*_bootstrap-1-arm64.hpkg | head -1)
[ -d develop ] || "$PKGTOOLS" extract "$FT"

# headers + real lib files into the sysroot (dissolve the /boot symlinks)
mkdir -p "$SYS/develop/headers/freetype2" "$SYS/develop/lib" "$SYS/lib"
cp -R develop/headers/freetype2/. "$SYS/develop/headers/freetype2/"
real=$(ls develop/lib/libfreetype.so.6.* | head -1)
cp -L "$real" "$SYS/develop/lib/libfreetype.so"
cp -L "$real" "$SYS/lib/libfreetype.so.6"

echo "deps installed: freetype2"
