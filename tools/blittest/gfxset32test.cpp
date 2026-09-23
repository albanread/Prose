// app_server's gfxset32, as the Haiku tree has it now, against the scalar loop
// it replaced (patch 0131), byte for byte: every length from -8 to 1100 bytes,
// at every alignment from 0 to 15, in four colours. Lengths that are not a
// multiple of 4 are included: callers never pass them, but they must not
// change either. A guard band on each side catches a store out of bounds.
//
// It builds on the Mac, which is arm64 like the guest, straight from the tree:
//
//	clang++ -O2 -Wall -Wextra -I shim \
//		-I /Volumes/HaikuSrc/haiku/src/servers/app/drawing \
//		-o gfxset32test gfxset32test.cpp && ./gfxset32test
#include <stdio.h>
#include <string.h>

#include "drawing_support.h"


// The loop gfxset32 was before, unchanged.
static void
scalar_gfxset32(uint8* dst, uint32 color, int32 numBytes)
{
	uint64 s64 = ((uint64)color << 32) | color;
	while (numBytes >= 8) {
		*(uint64*)dst = s64;
		numBytes -= 8;
		dst += 8;
	}
	if (numBytes == 4) {
		*(uint32*)dst = color;
	}
}


int
main()
{
	static const uint32 kColors[] = { 0x11223344, 0x00000000, 0xffffffff,
		0x80000001 };
	enum { kGuard = 64, kMaxBytes = 1100, kSize = kGuard + 16 + kMaxBytes + kGuard };
	alignas(16) static uint8 want[kSize];
	alignas(16) static uint8 got[kSize];

	int passed = 0;
	int total = 0;
	for (uint32 color : kColors) {
		int failures = 0;
		for (int32 offset = 0; offset < 16; offset++) {
			for (int32 numBytes = -8; numBytes <= kMaxBytes; numBytes++) {
				memset(want, 0xab, kSize);
				memset(got, 0xab, kSize);
				scalar_gfxset32(want + kGuard + offset, color, numBytes);
				gfxset32(got + kGuard + offset, color, numBytes);
				if (memcmp(want, got, kSize) != 0) {
					if (failures++ < 3) {
						printf("  colour %08x, offset %d, %d bytes: differs\n",
							color, offset, numBytes);
					}
				}
			}
		}
		total++;
		if (failures == 0)
			passed++;
		printf("%s: colour %08x, 16 offsets x %d lengths\n",
			failures == 0 ? "PASS" : "FAIL", color, kMaxBytes + 9);
	}

	printf("SELFTEST %s %d/%d\n", passed == total ? "PASS" : "FAIL", passed,
		total);
	return passed == total ? 0 : 1;
}
