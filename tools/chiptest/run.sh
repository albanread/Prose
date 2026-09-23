#!/bin/bash
# The chip's tests. Three of them, and each one found a real bug.
#
#   golden    24 ABC parses computed by hand: durations, tuplets, broken
#             rhythm, ties, chords, key signatures, tempo. It found four
#             places where MY EXPECTATIONS were wrong and none where the
#             parser was, which is what a golden test is for.
#   hashes    four of the Mojo gamepane's own committed FNV-1a hashes of
#             rendered effects. If the chip is a faithful port it reproduces
#             them bit for bit; it does.
#   player    the reference player (transcribed from chipplay.mojo) against
#             ours, sample by sample, over every motif the game ships. It
#             found the note lift and the one-based `v=`.
#
# Usage: tools/chiptest/run.sh [golden|hashes|player]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../hvgpu"
OUT="${TMPDIR:-/tmp}/chiptest"
mkdir -p "$OUT"
cd "$HERE"

# Swift allows top-level statements only in a file called main.swift, so each
# test is staged under that name before it is built.
build() { # name source extra-sources
	mkdir -p "$OUT/$1"
	cp "$HERE/$2" "$OUT/$1/main.swift"
	swiftc -O -target arm64-apple-macos26.0 -o "$OUT/$1/run" "$OUT/$1/main.swift" $3
}

want() { [ "${1:-all}" = all ] || [ "$1" = "$2" ]; }

if want "${1:-all}" golden; then
	build golden golden.swift "$SRC/abc.swift $SRC/chip.swift"
	"$OUT/golden/run"
fi
if want "${1:-all}" hashes; then
	build hashes hashes.swift "$SRC/chip.swift"
	"$OUT/hashes/run"
fi
if want "${1:-all}" player; then
	build player player.swift "$SRC/trio.swift $SRC/abc.swift $SRC/chip.swift"
	( cd "$HERE" && "$OUT/player/run" )
fi
