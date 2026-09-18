#!/bin/bash
# Sprint 0 / T5: the host-side Metal presenter (haiku_virtualized.md §3.1) on
# synthetic B_RGB32 surfaces. Each variant opens a window for a few seconds.
# A variant passes when the 1:1 self-test matches every pixel (colour intact,
# alpha forced to 255 although the surface's X bytes are 0) and presentation
# keeps up with the display link.
#
# Variants cover what a guest can hand us: page-aligned and 4 KiB-offset bases,
# tight and odd strides (1366*4 is not 64-byte aligned), padded strides, 4K.
# Output: work/t5/summary.txt. Exit 1 if any variant fails.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${WORK:-$ROOT/work}/t5"
"$ROOT/tools/build.sh"
rm -rf "$W"
mkdir -p "$W"

status=0
variant() { # name presenter-args...
	local name=$1 r
	shift
	if "$ROOT/build/bin/presenter" --seconds 4 "$@" > "$W/$name.log" 2>&1; then
		r=PASS
	else
		r=FAIL
		status=1
	fi
	printf '%-24s %s %s\n' "$name" "$r" "$(grep -o 'RESULT .*' "$W/$name.log" | cut -c8-)" | tee -a "$W/summary.txt"
}

variant 1280x800-aligned    --size 1280x800
variant 1366x768-odd-offset --size 1366x768 --offset 4096
variant 1920x1080-padded    --size 1920x1080 --stride 7936 --offset 4096
variant 3840x2160-offset    --size 3840x2160 --offset 4096
variant 1280x800-every-tick --size 1280x800 --present-always
echo
echo "T5 $([ $status -eq 0 ] && echo PASS || echo FAIL) (see $W/summary.txt)"
exit $status
