// libroot's memcpy, memmove and memset (Arm's optimized-routines on arm64
// since patch 0130) against byte-at-a-time references:
//
//   - every length from 0 to 520 at every source and destination alignment
//     from 0 to 31, which walks each size class (0-3, 4-7, 8-15, 16-32,
//     33-128, 129 up) and its boundaries
//   - overlapping moves in both directions, at distances up to 40 bytes and
//     around 64 and 128, where the large copy chooses its direction
//   - large lengths up to 16 MB, at odd offsets
//   - memset with zero over 128 bytes, which uses dc zva: on pages never
//     touched, and on copy-on-write pages after fork(), where the zeroing has
//     to fault as a write
//
// A guard band on each side of every destination catches a store out of
// bounds, and each call's return value is checked. It builds on the guest and
// on macOS (whose libc then is what is tested):
//
//	clang -O2 -Wall -Wextra -o stringtest stringtest.c && ./stringtest
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __HAIKU__
#	include <OS.h>
#endif

#define GUARD 64
#define SMALL_MAX 520

// Called through volatile pointers, so the compiler neither expands a call
// inline nor turns a reference loop into one.
typedef void* (*copy_fn)(void*, const void*, size_t);
typedef void* (*set_fn)(void*, int, size_t);
static copy_fn volatile sMemcpy = memcpy;
static copy_fn volatile sMemmove = memmove;
static set_fn volatile sMemset = memset;

static int sPassed;
static int sTotal;


static void
report(bool ok, const char* what)
{
	sTotal++;
	if (ok)
		sPassed++;
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
}


// The references write through volatile pointers, so that they cannot become
// calls to the functions under test.
static void
reference_move(uint8_t* to, const uint8_t* from, size_t length)
{
	volatile uint8_t* d = to;
	if (to < from) {
		for (size_t i = 0; i < length; i++)
			d[i] = from[i];
	} else {
		for (size_t i = length; i > 0; i--)
			d[i - 1] = from[i - 1];
	}
}


static void
reference_set(uint8_t* to, uint8_t value, size_t length)
{
	volatile uint8_t* d = to;
	for (size_t i = 0; i < length; i++)
		d[i] = value;
}


static void
pattern(uint8_t* buffer, size_t length, uint32_t seed)
{
	for (size_t i = 0; i < length; i++) {
		seed = seed * 1103515245 + 12345;
		buffer[i] = (uint8_t)(seed >> 16);
	}
}


static bool
check_small_copies(void)
{
	enum { kSize = GUARD + 32 + SMALL_MAX + GUARD };
	static uint8_t source[kSize];
	static uint8_t want[kSize];
	static uint8_t got[kSize];
	pattern(source, kSize, 1);

	int failures = 0;
	for (size_t length = 0; length <= SMALL_MAX; length++) {
		for (int from = 0; from < 32; from++) {
			for (int to = 0; to < 32; to++) {
				reference_set(want, 0xee, kSize);
				reference_set(got, 0xee, kSize);
				reference_move(want + GUARD + to, source + GUARD + from, length);
				void* result = sMemcpy(got + GUARD + to, source + GUARD + from,
					length);
				if (result != got + GUARD + to
					|| memcmp(want, got, kSize) != 0) {
					if (failures++ < 3) {
						printf("  memcpy %zu bytes, source +%d, destination +%d\n",
							length, from, to);
					}
				}
			}
		}
	}
	return failures == 0;
}


static bool
check_moves(size_t maxLength, const int* distances, int distanceCount,
	int alignments)
{
	size_t size = GUARD + 256 + maxLength + 256 + GUARD;
	uint8_t* want = malloc(size);
	uint8_t* got = malloc(size);
	int failures = 0;

	for (size_t length = 0; length <= maxLength; length++) {
		for (int align = 0; align < alignments; align++) {
			for (int i = 0; i < distanceCount; i++) {
				size_t from = GUARD + 256 + align;
				size_t to = from + distances[i];
				pattern(want, size, (uint32_t)length);
				pattern(got, size, (uint32_t)length);
				reference_move(want + to, want + from, length);
				void* result = sMemmove(got + to, got + from, length);
				if (result != got + to || memcmp(want, got, size) != 0) {
					if (failures++ < 3) {
						printf("  memmove %zu bytes, +%d, distance %d\n",
							length, align, distances[i]);
					}
				}
			}
		}
	}
	free(want);
	free(got);
	return failures == 0;
}


static bool
check_small_sets(void)
{
	enum { kSize = GUARD + 32 + SMALL_MAX + GUARD };
	static const int kValues[] = { 0x00, 0x5a, 0xff, 0x100, 0x180 };
	static uint8_t want[kSize];
	static uint8_t got[kSize];

	int failures = 0;
	for (size_t length = 0; length <= SMALL_MAX; length++) {
		for (int to = 0; to < 32; to++) {
			for (size_t v = 0; v < sizeof(kValues) / sizeof(kValues[0]); v++) {
				pattern(want, kSize, (uint32_t)length);
				pattern(got, kSize, (uint32_t)length);
				// memset uses the value converted to unsigned char
				reference_set(want + GUARD + to, (uint8_t)kValues[v], length);
				void* result = sMemset(got + GUARD + to, kValues[v], length);
				if (result != got + GUARD + to
					|| memcmp(want, got, kSize) != 0) {
					if (failures++ < 3) {
						printf("  memset %zu bytes of %#x at +%d\n", length,
							kValues[v], to);
					}
				}
			}
		}
	}
	return failures == 0;
}


static bool
check_large(void)
{
	static const size_t kLengths[] = { 1000, 4095, 4096, 4097, 65536 + 13,
		(1 << 20) + 7, (16 << 20) + 3 };
	static const int kOffsets[] = { 0, 1, 4, 13, 63 };
	static const int kDistances[] = { -4096, -64, -16, -4, -1, 1, 4, 16, 64,
		4096 };
	int failures = 0;

	for (size_t l = 0; l < sizeof(kLengths) / sizeof(kLengths[0]); l++) {
		size_t length = kLengths[l];
		size_t size = GUARD + 8192 + length + 8192 + GUARD;
		uint8_t* source = malloc(size);
		uint8_t* want = malloc(size);
		uint8_t* got = malloc(size);
		if (source == NULL || want == NULL || got == NULL)
			return false;

		// a megabyte and more: two offsets are enough, and quicker
		size_t offsets = length < (1 << 20) ? 5 : 2;
		for (size_t o = 0; o < offsets; o++) {
			int offset = o == 1 && offsets == 2 ? 13 : kOffsets[o];
			size_t at = GUARD + 8192 + offset;

			// copy, from a differently aligned place
			pattern(source, size, (uint32_t)(length + o));
			reference_set(want, 0xee, size);
			reference_set(got, 0xee, size);
			reference_move(want + at, source + at + 3, length);
			if (sMemcpy(got + at, source + at + 3, length) != got + at
				|| memcmp(want, got, size) != 0) {
				if (failures++ < 3)
					printf("  memcpy %zu bytes at +%d\n", length, offset);
			}

			// overlapping moves, both ways
			for (size_t d = 0; d < sizeof(kDistances) / sizeof(kDistances[0]);
					d++) {
				pattern(want, size, (uint32_t)(length + d));
				pattern(got, size, (uint32_t)(length + d));
				reference_move(want + at + kDistances[d], want + at, length);
				if (sMemmove(got + at + kDistances[d], got + at, length)
						!= got + at + kDistances[d]
					|| memcmp(want, got, size) != 0) {
					if (failures++ < 3) {
						printf("  memmove %zu bytes at +%d, distance %d\n",
							length, offset, kDistances[d]);
					}
				}
			}

			// fills, zero among them
			for (int value = 0; value < 0x100; value += 0xa5) {
				pattern(want, size, (uint32_t)(length + value));
				pattern(got, size, (uint32_t)(length + value));
				reference_set(want + at, (uint8_t)value, length);
				if (sMemset(got + at, value, length) != got + at
					|| memcmp(want, got, size) != 0) {
					if (failures++ < 3) {
						printf("  memset %zu bytes of %#x at +%d\n", length,
							value, offset);
					}
				}
			}
		}
		free(source);
		free(want);
		free(got);
	}
	return failures == 0;
}


static bool
all_zero(const uint8_t* buffer, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		if (buffer[i] != 0)
			return false;
	}
	return true;
}


// Zeroing pages that were never touched: every other page is written first,
// so the zeroing meets both mapped and unmapped pages, and has to fault the
// unmapped ones in as a write.
static bool
check_zero_fresh_pages(void)
{
	size_t pageSize = (size_t)sysconf(_SC_PAGESIZE);
	size_t length = 64 * pageSize;
	uint8_t* pages = mmap(NULL, length, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pages == MAP_FAILED)
		return false;
	for (size_t page = 0; page < 64; page += 2)
		memset(pages + page * pageSize, 0x77, pageSize);

	bool ok = sMemset(pages + 5, 0, length - 10) == pages + 5
		&& all_zero(pages + 5, length - 10)
		&& pages[0] == 0x77 && pages[4] == 0x77;
	munmap(pages, length);

#ifdef __HAIKU__
	// the same through an area, created lazily, as Haiku programs do
	void* address;
	area_id area = create_area("stringtest", &address, B_ANY_ADDRESS, length,
		B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	if (area < 0)
		return false;
	uint8_t* bytes = address;
	bytes[length / 2] = 0x33;
	ok = ok && sMemset(bytes, 0, length) == bytes && all_zero(bytes, length);
	delete_area(area);
#endif
	return ok;
}


// Zeroing copy-on-write pages: after fork() the child's pages are the
// parent's, shared, until one of them writes. The child zeroes them; it must
// see zeroes and the parent must still see its own data.
static bool
check_zero_copy_on_write(void)
{
	size_t length = 1 << 20;
	uint8_t* buffer = malloc(length);
	if (buffer == NULL)
		return false;
	pattern(buffer, length, 99);
	uint8_t first = buffer[1000];
	uint8_t last = buffer[length - 1000];

	// or the child can print the parent's unwritten lines a second time
	fflush(stdout);
	pid_t child = fork();
	if (child < 0) {
		free(buffer);
		return false;
	}
	if (child == 0) {
		sMemset(buffer + 1, 0, length - 2);
		_exit(all_zero(buffer + 1, length - 2) ? 0 : 1);
	}

	int status = 0;
	waitpid(child, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0
		&& buffer[1000] == first && buffer[length - 1000] == last;
	free(buffer);
	return ok;
}


int
main(void)
{
	static const int kNear[] = { -40, -33, -32, -31, -17, -16, -15, -9, -8,
		-7, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31,
		32, 33, 40 };
	static const int kFar[] = { -200, -129, -128, -127, -100, -65, -64, -63,
		63, 64, 65, 100, 127, 128, 129, 200 };

	report(check_small_copies(),
		"memcpy, 0-520 bytes, every source and destination alignment 0-31");
	report(check_moves(SMALL_MAX, kNear, sizeof(kNear) / sizeof(kNear[0]), 16),
		"memmove, 0-520 bytes, overlapping by up to 40 either way, alignments 0-15");
	report(check_moves(SMALL_MAX, kFar, sizeof(kFar) / sizeof(kFar[0]), 4),
		"memmove, 0-520 bytes, 63-200 bytes apart either way");
	report(check_small_sets(),
		"memset, 0-520 bytes, alignments 0-31, zero and not");
	report(check_large(),
		"large lengths to 16 MB: memcpy, memmove both ways, memset");
	report(check_zero_fresh_pages(), "memset zero over pages never touched");
	report(check_zero_copy_on_write(),
		"memset zero over copy-on-write pages after fork()");

	printf("SELFTEST %s %d/%d\n", sPassed == sTotal ? "PASS" : "FAIL", sPassed,
		sTotal);
	return sPassed == sTotal ? 0 : 1;
}
