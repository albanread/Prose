// Blitting: what app_server's copies and fills achieve, against the same work
// done sixteen bytes at a time in vector registers. The Prose accelerant has
// no 2D hooks, so app_server moves every pixel itself: it copies the back
// buffer to the front one row by row with memcpy (HWInterface::_CopyToFront),
// scrolls with memcpy/memmove per row (DrawingEngine::_CopyRect), and fills
// with gfxset32 (drawing_support.h). Those, and libroot's memset, are the
// instructions that move pixels.
//
// The default size is one 1920x1080x32 frame (8 MB), which fits in an M4 Max
// core's L2; give a size in MB to measure past the caches. Each figure is the
// best of five trials after half a second of warm-up, so the core is at full
// clock. It builds on macOS too, as a reference for the same core.
#include <arm_neon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __HAIKU__
#	include <OS.h>
#else
#	include <pthread.h>
#	include <time.h>

typedef int64_t bigtime_t;

static bigtime_t
system_time(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (bigtime_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
#endif

#ifdef __clang__
#	define SCALAR_LOOP \
		_Pragma("clang loop vectorize(disable) interleave(disable) unroll(disable)")
#else
#	define SCALAR_LOOP
#endif

#define FRAME_BYTES (1920 * 1080 * 4)
#define TRIALS 5


static void
neon_copy(uint8_t* to, const uint8_t* from, size_t length)
{
	for (size_t i = 0; i + 64 <= length; i += 64)
		vst1q_u8_x4(to + i, vld1q_u8_x4(from + i));
}


// Backwards, for an overlapping move to a higher address: each block is loaded
// whole before it is stored, and no store reaches a block still to be loaded.
static void
neon_move_up(uint8_t* to, const uint8_t* from, size_t length)
{
	for (size_t i = length; i >= 64; i -= 64)
		vst1q_u8_x4(to + i - 64, vld1q_u8_x4(from + i - 64));
}


static void
neon_fill(uint8_t* to, uint32_t color, size_t length)
{
	uint32x4_t v = vdupq_n_u32(color);
	uint32x4x4_t block = { { v, v, v, v } };
	for (size_t i = 0; i + 64 <= length; i += 64)
		vst1q_u32_x4((uint32_t*)(to + i), block);
}


// app_server's fill as drawing_support.h has it and as GCC builds it there:
// one 8-byte store per iteration. SCALAR_LOOP keeps clang from vectorizing
// it, which GCC at -O2 does not do either.
static void
gfxset32(uint8_t* dst, uint32_t color, int32_t numBytes)
{
	uint64_t s64 = ((uint64_t)color << 32) | color;
	SCALAR_LOOP
	while (numBytes >= 8) {
		*(uint64_t*)dst = s64;
		numBytes -= 8;
		dst += 8;
	}
	if (numBytes == 4)
		*(uint32_t*)dst = color;
}


// Zeroes whole blocks without reading them first; only usable when the kernel
// allows it at EL0 (DCZID_EL0.DZP clear), and only for zero.
static void
zva_zero(uint8_t* to, size_t length, size_t block)
{
	for (size_t i = 0; i + block <= length; i += block)
		__asm__ volatile("dc zva, %0" : : "r"(to + i) : "memory");
}


// Called through volatile pointers, with a clobber after each call, so the
// compiler cannot see what they do and cannot drop a repeat as redundant.
typedef void* (*copy_fn)(void*, const void*, size_t);
typedef void* (*set_fn)(void*, int, size_t);
static copy_fn volatile pMemcpy = memcpy;
static copy_fn volatile pMemmove = memmove;
static set_fn volatile pMemset = memset;
static void (*volatile pNeonCopy)(uint8_t*, const uint8_t*, size_t) = neon_copy;
static void (*volatile pNeonMoveUp)(uint8_t*, const uint8_t*, size_t) = neon_move_up;
static void (*volatile pNeonFill)(uint8_t*, uint32_t, size_t) = neon_fill;
static void (*volatile pGfxset32)(uint8_t*, uint32_t, int32_t) = gfxset32;
static void (*volatile pZvaZero)(uint8_t*, size_t, size_t) = zva_zero;

enum {
	kMemcpy,
	kMemcpyOffset,
	kMemmoveScroll,
	kNeonCopy,
	kNeonCopyOffset,
	kNeonMoveScroll,
	kMemset,
	kGfxset32,
	kNeonFill,
	kMemsetZero,
	kZvaZero,
	kTestCount
};

static const char* const kTestNames[kTestCount] = {
	"memcpy, aligned",
	"memcpy, off by one pixel",
	"memmove, scroll one pixel",
	"neon copy, aligned",
	"neon copy, off by one pixel",
	"neon move, scroll one pixel",
	"memset",
	"gfxset32 (app_server fill)",
	"neon fill",
	"memset, zero",
	"dc zva, zero",
};

static uint8_t* sA;
static uint8_t* sB;
static size_t sLength;
static size_t sZvaBlock;


static void
run(int test)
{
	switch (test) {
		case kMemcpy:
			pMemcpy(sB, sA, sLength);
			break;
		case kMemcpyOffset:
			pMemcpy(sB, sA + 4, sLength);
			break;
		case kMemmoveScroll:
			pMemmove(sA + 4, sA, sLength);
			break;
		case kNeonCopy:
			pNeonCopy(sB, sA, sLength);
			break;
		case kNeonCopyOffset:
			pNeonCopy(sB, sA + 4, sLength);
			break;
		case kNeonMoveScroll:
			pNeonMoveUp(sA + 4, sA, sLength);
			break;
		case kMemset:
			pMemset(sB, 0x55, sLength);
			break;
		case kGfxset32:
			pGfxset32(sB, 0x55555555, (int32_t)sLength);
			break;
		case kNeonFill:
			pNeonFill(sB, 0x55555555, sLength);
			break;
		case kMemsetZero:
			pMemset(sB, 0, sLength);
			break;
		case kZvaZero:
			pZvaZero(sB, sLength, sZvaBlock);
			break;
	}
	__asm__ volatile("" : : : "memory");
}


static double
best_rate(int test, int reps)
{
	double best = 0;
	for (int trial = 0; trial < TRIALS; trial++) {
		bigtime_t start = system_time();
		for (int r = 0; r < reps; r++)
			run(test);
		double rate = (double)sLength * reps / (system_time() - start) / 1000.0;
		if (rate > best)
			best = rate;
	}
	return best;
}


// The vector loops must do what the library calls do, or their speed means
// nothing; checked on odd offsets and against the library for every result.
static bool
check(size_t zvaBlock)
{
	enum { kSize = 65536 };
	uint8_t* source = aligned_alloc(64, kSize + 128);
	uint8_t* want = aligned_alloc(64, kSize + 128);
	uint8_t* got = aligned_alloc(64, kSize + 128);
	bool ok = true;

	for (size_t i = 0; i < kSize + 128; i++)
		source[i] = (uint8_t)(i * 7 + 3);

	memcpy(want, source + 4, kSize);
	neon_copy(got, source + 4, kSize);
	ok &= memcmp(want, got, kSize) == 0;

	memcpy(want, source, kSize + 128);
	memcpy(got, source, kSize + 128);
	memmove(want + 4, want, kSize);
	neon_move_up(got + 4, got, kSize);
	ok &= memcmp(want, got, kSize + 128) == 0;

	for (size_t i = 0; i < kSize / 4; i++)
		((uint32_t*)want)[i] = 0x11223344;
	neon_fill(got, 0x11223344, kSize);
	ok &= memcmp(want, got, kSize) == 0;
	memset(got, 0, kSize);
	gfxset32(got, 0x11223344, kSize);
	ok &= memcmp(want, got, kSize) == 0;

	if (zvaBlock != 0) {
		memset(want, 0, kSize);
		memset(got, 0xff, kSize);
		zva_zero(got, kSize, zvaBlock);
		ok &= memcmp(want, got, kSize) == 0;
	}

	free(source);
	free(want);
	free(got);
	return ok;
}


int
main(int argc, char** argv)
{
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	sLength = FRAME_BYTES;
	if (argc > 1)
		sLength = (size_t)atoi(argv[1]) * 1024 * 1024;
	if (sLength == 0 || sLength > 1024 * 1024 * 1024 || sLength % 64 != 0) {
		fprintf(stderr, "usage: %s [MB, 1 to 1024]\n", argv[0]);
		return 2;
	}

	uint64_t dczid;
	__asm__("mrs %0, dczid_el0" : "=r"(dczid));
	sZvaBlock = (dczid & (1 << 4)) != 0 ? 0 : (size_t)4 << (dczid & 0xf);

	bool ok = check(sZvaBlock);
	printf("vector loops agree with the library: %s\n", ok ? "PASS" : "FAIL");
	if (!ok)
		return 1;

	sA = aligned_alloc(64, sLength + 64);
	sB = aligned_alloc(64, sLength + 64);
	if (sA == NULL || sB == NULL) {
		fprintf(stderr, "cannot allocate 2 x %zu bytes\n", sLength + 64);
		return 1;
	}
	memset(sA, 1, sLength + 64);
	memset(sB, 2, sLength + 64);

	bigtime_t warm = system_time();
	while (system_time() - warm < 500000)
		run(kNeonCopy);

	int reps = (int)(256 * 1024 * 1024 / sLength);
	if (reps < 1)
		reps = 1;
	printf("%.1f MB, best of %d trials of %d, dc zva ",
		sLength / (1024.0 * 1024.0), TRIALS, reps);
	if (sZvaBlock != 0)
		printf("allowed in %zu-byte blocks\n", sZvaBlock);
	else
		printf("prohibited at EL0\n");

	for (int test = 0; test < kTestCount; test++) {
		if (test == kZvaZero && sZvaBlock == 0)
			continue;
		printf("%-30s %7.2f GB/s\n", kTestNames[test], best_rate(test, reps));
	}
	return 0;
}
