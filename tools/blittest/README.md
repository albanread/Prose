# Blitting tests

The regression tests for patches 0130 (libroot's arm64 `memcpy`, `memmove` and
`memset` from Arm's optimized-routines) and 0131 (app_server's `gfxset32` in
vector stores). Each prints PASS or FAIL per check and `SELFTEST PASS n/n`.

| test | runs on | what it proves |
|---|---|---|
| `stringtest.c` | guest; the Mac as a control | `memcpy`, `memmove` and `memset` against byte-at-a-time references: every length 0-520 at every alignment 0-31, overlaps both ways, lengths to 16 MB, and `dc zva` zeroing over pages never touched and over copy-on-write pages after `fork()` |
| `blittest.cpp` | guest | app_server's own paths, through the real server into a `BBitmap`: `FillRect` and `FillRegion` (gfxset32), `CopyBits` sideways and between rows (`_CopyRect`'s memmove, and its memcpy over its own source), `DrawBitmap` of a 37-pixel bitmap at odd and even x (`DrawBitmapNoScale`'s memcpy); every pixel read back |
| `gfxset32test.cpp` | the Mac | the tree's `gfxset32`, from `drawing_support.h` itself, byte for byte against the loop it replaced, for lengths -8 to 1100 at 16 alignments |

In the guest (boot one machine with `scripts/run-machine.sh`; a source file is
larger than one portal request, so send it in pieces):

	clang -O2 -Wall -Wextra -o stringtest stringtest.c && ./stringtest
	clang++ -O2 -Wall -Wextra -o blittest blittest.cpp -lbe && ./blittest

On the Mac:

	clang++ -std=c++17 -O2 -Wall -Wextra -Wno-vla-cxx-extension -I shim \
		-I /Volumes/HaikuSrc/haiku/src/servers/app/drawing \
		-o gfxset32test gfxset32test.cpp && ./gfxset32test

(`-Wno-vla-cxx-extension` is for `blend_line32`, further down the same header.)

## Results (2026-09-23)

- `stringtest`: 7/7 on the image before 0130 (the control), 7/7 after, 7/7
  against macOS's libc.
- `blittest`: 5/5 before 0130 and 0131 (the control), 5/5 after.
- `gfxset32test`: 4/4.
- The desktop after 0130 and 0131 is the one before, pixel for pixel, but for
  the clock, the Deskbar's tray and the free-space bar on the disk icon.
