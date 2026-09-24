/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	malloctest: the regression test for libroot's allocator.

	Every size class and region size, filled and verified while all are live;
	calloc after reuse; realloc across every boundary; the alignment entry
	points; malloc_usable_size; frees from other threads; threads that exit
	leaving blocks behind; fork while other threads allocate; and memory
	given back after a large free.

		malloctest [--crash]

	--crash adds the double-free test, which needs the debug_server to kill
	crashing teams (default_action kill) rather than wait at an alert.
*/

#include <OS.h>

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


static int sPassed = 0;
static int sTotal = 0;


static void
check(bool ok, const char* name, const char* detail)
{
	sTotal++;
	if (ok)
		sPassed++;
	printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		detail != NULL && detail[0] != '\0' ? ": " : "",
		detail != NULL ? detail : "");
	fflush(stdout);
}


static uint64_t sRandom = 0x9e3779b97f4a7c15ULL;


static uint64_t
random64(uint64_t* state)
{
	// xorshift64*
	uint64_t x = *state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * 0x2545f4914f6cdd1dULL;
}


static uint8_t
pattern_byte(uintptr_t seed, size_t offset)
{
	return (uint8_t)((seed * 131 + offset * 7 + (offset >> 8)) & 0xff);
}


/*!	Fills a block so that any overlap with another block, or any byte the
	allocator writes into it, shows. Big blocks get one byte a page and
	their first and last KB. */
static void
fill(uint8_t* block, size_t size, uintptr_t seed)
{
	if (size <= 65536) {
		for (size_t i = 0; i < size; i++)
			block[i] = pattern_byte(seed, i);
		return;
	}
	for (size_t i = 0; i < 1024; i++) {
		block[i] = pattern_byte(seed, i);
		block[size - 1 - i] = pattern_byte(seed, size - 1 - i);
	}
	for (size_t i = 4096; i < size; i += 4096)
		block[i] = pattern_byte(seed, i);
}


static bool
verify(const uint8_t* block, size_t size, uintptr_t seed)
{
	if (size <= 65536) {
		for (size_t i = 0; i < size; i++) {
			if (block[i] != pattern_byte(seed, i))
				return false;
		}
		return true;
	}
	for (size_t i = 0; i < 1024; i++) {
		if (block[i] != pattern_byte(seed, i)
			|| block[size - 1 - i] != pattern_byte(seed, size - 1 - i))
			return false;
	}
	for (size_t i = 4096; i < size; i += 4096) {
		if (block[i] != pattern_byte(seed, i))
			return false;
	}
	return true;
}


/*!	Checks the bytes fill() wrote below \a kept, in a block filled at
	\a size bytes. */
static bool
verify_prefix(const uint8_t* block, size_t size, size_t kept, uintptr_t seed)
{
	for (size_t i = 0; i < kept; i++) {
		bool written = size <= 65536 || i < 1024 || i >= size - 1024
			|| (i >= 4096 && i % 4096 == 0);
		if (written && block[i] != pattern_byte(seed, i))
			return false;
	}
	return true;
}


static void
shuffle(void** items, size_t count, uint64_t* state)
{
	for (size_t i = count - 1; i > 0; i--) {
		size_t j = random64(state) % (i + 1);
		void* t = items[i];
		items[i] = items[j];
		items[j] = t;
	}
}


/*!	The team's resident memory: the RAM of all its areas. */
static uint64_t
team_resident(int32* _areas)
{
	uint64_t resident = 0;
	int32 areas = 0;
	ssize_t cookie = 0;
	area_info info;
	while (get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		resident += info.ram_size;
		areas++;
	}
	if (_areas != NULL)
		*_areas = areas;
	return resident;
}


// #pragma mark - sizes


static const size_t kSizes[] = {
	0, 1, 7, 8, 15, 16, 17, 24, 31, 32, 33, 48, 63, 64, 65, 80, 96, 112, 127,
	128, 129, 160, 192, 224, 255, 256, 257, 320, 384, 448, 511, 512, 513,
	640, 768, 896, 1023, 1024, 1025, 1280, 1536, 1792, 2047, 2048, 2049,
	2560, 3072, 3584, 4095, 4096, 4097, 5120, 6144, 7168, 8191, 8192, 8193,
	12288, 16383, 16384, 16385, 24576, 32767, 32768, 32769, 65535, 65536,
	65537, 131072, 262144, 262145, 1048576, 2097153, 8388609, 33554432,
	67108865
};


static void
test_sizes()
{
	uint64_t state = sRandom;
	size_t total = 0;
	for (size_t s = 0; s < B_COUNT_OF(kSizes); s++) {
		size_t size = kSizes[s];
		total += size <= 4096 ? 200 : size <= 65536 ? 40 : size <= 2097153 ? 8
			: 2;
	}

	void** blocks = malloc(total * sizeof(void*));
	size_t* sizes = malloc(total * sizeof(size_t));
	size_t count = 0;
	bool allocated = true;
	bool aligned = true;
	bool usable = true;
	char detail[160] = "";
	for (size_t s = 0; s < B_COUNT_OF(kSizes); s++) {
		size_t size = kSizes[s];
		size_t n = size <= 4096 ? 200 : size <= 65536 ? 40
			: size <= 2097153 ? 8 : 2;
		for (size_t i = 0; i < n; i++) {
			uint8_t* block = malloc(size);
			if (block == NULL) {
				allocated = false;
				snprintf(detail, sizeof(detail), "malloc(%zu) failed", size);
				break;
			}
			if (((uintptr_t)block & 15) != 0 && size >= 16) {
				aligned = false;
				snprintf(detail, sizeof(detail), "malloc(%zu) = %p", size,
					block);
			}
			if (malloc_usable_size(block) < size) {
				usable = false;
				snprintf(detail, sizeof(detail),
					"malloc_usable_size(malloc(%zu)) = %zu", size,
					malloc_usable_size(block));
			}
			fill(block, size, (uintptr_t)count);
			blocks[count] = block;
			sizes[count] = size;
			count++;
		}
	}

	bool intact = true;
	for (size_t i = 0; i < count; i++) {
		if (!verify(blocks[i], sizes[i], (uintptr_t)i)) {
			intact = false;
			snprintf(detail, sizeof(detail), "block %zu (%zu bytes at %p) "
				"was overwritten", i, sizes[i], blocks[i]);
			break;
		}
	}

	// free in a random order, keeping the sizes with their blocks
	size_t* order = malloc(count * sizeof(size_t));
	for (size_t i = 0; i < count; i++)
		order[i] = i;
	for (size_t i = count - 1; i > 0; i--) {
		size_t j = random64(&state) % (i + 1);
		size_t t = order[i];
		order[i] = order[j];
		order[j] = t;
	}
	for (size_t i = 0; i < count; i++) {
		size_t k = order[i];
		if (!verify(blocks[k], sizes[k], (uintptr_t)k)) {
			intact = false;
			snprintf(detail, sizeof(detail), "block %zu (%zu bytes) was "
				"overwritten while others were freed", k, sizes[k]);
		}
		free(blocks[k]);
	}
	free(order);
	free(blocks);
	free(sizes);

	check(allocated && aligned && usable && intact, "sizes",
		detail[0] != '\0' ? detail : "every size class and region size, "
			"live together, filled and verified");
}


// #pragma mark - calloc


static void
test_calloc()
{
	static const size_t kCallocSizes[] = { 1, 16, 100, 1000, 2048, 4000,
		8192, 10000, 16384, 50000, 262144, 1048576, 16777216 };
	bool zero = true;
	char detail[128] = "";
	for (int round = 0; round < 3; round++) {
		// dirty blocks of each size, free them, calloc the same sizes
		void* dirty[B_COUNT_OF(kCallocSizes) * 4];
		for (size_t i = 0; i < B_COUNT_OF(dirty); i++) {
			size_t size = kCallocSizes[i % B_COUNT_OF(kCallocSizes)];
			dirty[i] = malloc(size);
			memset(dirty[i], 0xab, size);
		}
		for (size_t i = 0; i < B_COUNT_OF(dirty); i++)
			free(dirty[i]);

		for (size_t i = 0; i < B_COUNT_OF(dirty); i++) {
			size_t size = kCallocSizes[i % B_COUNT_OF(kCallocSizes)];
			uint8_t* block = calloc(1, size);
			for (size_t j = 0; j < size; j++) {
				if (block[j] != 0) {
					zero = false;
					snprintf(detail, sizeof(detail),
						"calloc(%zu) byte %zu = %#x", size, j, block[j]);
					break;
				}
			}
			dirty[i] = block;
		}
		for (size_t i = 0; i < B_COUNT_OF(dirty); i++)
			free(dirty[i]);
	}

	// volatile, so the compiler does not see the overflow coming
	volatile size_t huge = SIZE_MAX / 2;
	errno = 0;
	void* overflow = calloc(huge, 4);
	bool refused = overflow == NULL && errno == ENOMEM;
	free(overflow);

	check(zero && refused, "calloc", detail[0] != '\0' ? detail
		: !refused ? "calloc(SIZE_MAX / 2, 4) did not fail with ENOMEM"
		: "zeroed after reuse, overflow refused");
}


// #pragma mark - realloc


static void
test_realloc()
{
	static const size_t kSteps[] = { 1, 16, 17, 100, 2048, 2049, 4096, 8192,
		8193, 16384, 16385, 40000, 65536, 300000, 1048577, 5000000 };
	bool intact = true;
	char detail[128] = "";
	for (size_t a = 0; a < B_COUNT_OF(kSteps) && intact; a++) {
		for (size_t b = 0; b < B_COUNT_OF(kSteps); b++) {
			size_t from = kSteps[a];
			size_t to = kSteps[b];
			uint8_t* block = malloc(from);
			fill(block, from, from ^ to);
			// something after it, so growing in place is not always free
			void* neighbour = malloc(from);
			uint8_t* moved = realloc(block, to);
			if (moved == NULL) {
				intact = false;
				snprintf(detail, sizeof(detail), "realloc(%zu -> %zu) failed",
					from, to);
				free(block);
				free(neighbour);
				break;
			}
			size_t kept = from < to ? from : to;
			bool ok = verify_prefix(moved, from, kept, from ^ to);
			if (!ok) {
				intact = false;
				snprintf(detail, sizeof(detail),
					"realloc(%zu -> %zu) lost the contents", from, to);
			}
			memset(moved, 0x5a, to);
			free(moved);
			free(neighbour);
		}
	}

	// growing one block step by step, as a vector does
	uint8_t* vector = NULL;
	size_t length = 0;
	for (size_t size = 16; size <= 8 * 1024 * 1024 && intact;
			size += size / 2) {
		vector = realloc(vector, size);
		for (size_t i = length; i < size; i++)
			vector[i] = (uint8_t)(i * 13);
		for (size_t i = 0; i < size; i += 997) {
			if (vector[i] != (uint8_t)(i * 13)) {
				intact = false;
				snprintf(detail, sizeof(detail),
					"growing vector lost byte %zu at %zu bytes", i, size);
				break;
			}
		}
		length = size;
	}
	free(vector);

	void* fromNull = realloc(NULL, 100);
	bool nullOk = fromNull != NULL;
	free(fromNull);

	check(intact && nullOk, "realloc", detail[0] != '\0' ? detail
		: "contents kept across every size boundary, growing and shrinking");
}


// #pragma mark - alignment


static void
test_alignment()
{
	static const size_t kAlignSizes[] = { 1, 7, 16, 100, 4096, 10000,
		70000 };
	bool ok = true;
	char detail[128] = "";
	for (size_t alignment = sizeof(void*); alignment <= 1024 * 1024;
			alignment *= 2) {
		for (size_t s = 0; s < B_COUNT_OF(kAlignSizes); s++) {
			size_t size = kAlignSizes[s];
			void* block = NULL;
			int error = posix_memalign(&block, alignment, size);
			if (error != 0 || ((uintptr_t)block & (alignment - 1)) != 0
				|| malloc_usable_size(block) < size) {
				ok = false;
				snprintf(detail, sizeof(detail),
					"posix_memalign(%zu, %zu) = %d, %p", alignment, size,
					error, block);
			} else
				memset(block, 0x33, size);
			free(block);

			block = memalign(alignment, size);
			if (block == NULL || ((uintptr_t)block & (alignment - 1)) != 0) {
				ok = false;
				snprintf(detail, sizeof(detail), "memalign(%zu, %zu) = %p",
					alignment, size, block);
			} else
				memset(block, 0x44, size);
			free(block);

			size_t whole = (size + alignment - 1) & ~(alignment - 1);
			block = aligned_alloc(alignment, whole);
			if (block == NULL || ((uintptr_t)block & (alignment - 1)) != 0) {
				ok = false;
				snprintf(detail, sizeof(detail),
					"aligned_alloc(%zu, %zu) = %p", alignment, whole, block);
			} else
				memset(block, 0x55, whole);
			free(block);
		}
	}

	void* page = valloc(10);
	if (page == NULL || ((uintptr_t)page & (B_PAGE_SIZE - 1)) != 0) {
		ok = false;
		snprintf(detail, sizeof(detail), "valloc(10) = %p", page);
	}
	free(page);

	void* bad = NULL;
	if (posix_memalign(&bad, 24, 100) != EINVAL) {
		ok = false;
		snprintf(detail, sizeof(detail), "posix_memalign(24) accepted");
	}

	check(ok, "alignment", detail[0] != '\0' ? detail
		: "posix_memalign, memalign, aligned_alloc and valloc, up to 1 MB");
}


// #pragma mark - cross-thread frees


#define QUEUE_SIZE 4096

typedef struct {
	pthread_mutex_t	lock;
	pthread_cond_t	changed;
	void*			items[QUEUE_SIZE];
	size_t			sizes[QUEUE_SIZE];
	int				head;
	int				tail;
	int				producersLeft;
} queue;


static void
queue_put(queue* q, void* item, size_t size)
{
	pthread_mutex_lock(&q->lock);
	while ((q->tail + 1) % QUEUE_SIZE == q->head)
		pthread_cond_wait(&q->changed, &q->lock);
	q->items[q->tail] = item;
	q->sizes[q->tail] = size;
	q->tail = (q->tail + 1) % QUEUE_SIZE;
	pthread_cond_broadcast(&q->changed);
	pthread_mutex_unlock(&q->lock);
}


static bool
queue_get(queue* q, void** item, size_t* size)
{
	pthread_mutex_lock(&q->lock);
	while (q->head == q->tail && q->producersLeft > 0)
		pthread_cond_wait(&q->changed, &q->lock);
	if (q->head == q->tail) {
		pthread_mutex_unlock(&q->lock);
		return false;
	}
	*item = q->items[q->head];
	*size = q->sizes[q->head];
	q->head = (q->head + 1) % QUEUE_SIZE;
	pthread_cond_broadcast(&q->changed);
	pthread_mutex_unlock(&q->lock);
	return true;
}


static queue sQueue;
static int32 sBadBlocks = 0;
static int32 sConsumed = 0;


static void*
producer(void* data)
{
	uint64_t state = (uintptr_t)data * 0x100000001b3ULL + 1;
	for (int i = 0; i < 20000; i++) {
		uint64_t r = random64(&state);
		size_t size = (r & 7) == 0 ? 16 + r % 20000 : 16 + r % 600;
		uint8_t* block = malloc(size);
		fill(block, size, size * 3 + 1);
		// some blocks this thread frees itself, interleaved
		if ((r & 31) == 0) {
			void* own = malloc(64);
			free(own);
		}
		queue_put(&sQueue, block, size);
	}
	pthread_mutex_lock(&sQueue.lock);
	sQueue.producersLeft--;
	pthread_cond_broadcast(&sQueue.changed);
	pthread_mutex_unlock(&sQueue.lock);
	return NULL;
}


static void*
consumer(void* data)
{
	(void)data;
	void* block;
	size_t size;
	while (queue_get(&sQueue, &block, &size)) {
		if (!verify(block, size, size * 3 + 1))
			atomic_add(&sBadBlocks, 1);
		// and allocate a little in this thread too
		void* mine = malloc(size / 2 + 1);
		free(block);
		free(mine);
		atomic_add(&sConsumed, 1);
	}
	return NULL;
}


static void
test_cross_thread()
{
	enum { kProducers = 4, kConsumers = 4 };
	pthread_mutex_init(&sQueue.lock, NULL);
	pthread_cond_init(&sQueue.changed, NULL);
	sQueue.head = sQueue.tail = 0;
	sQueue.producersLeft = kProducers;

	pthread_t threads[kProducers + kConsumers];
	for (int i = 0; i < kProducers; i++)
		pthread_create(&threads[i], NULL, producer, (void*)(intptr_t)i);
	for (int i = 0; i < kConsumers; i++)
		pthread_create(&threads[kProducers + i], NULL, consumer, NULL);
	for (int i = 0; i < kProducers + kConsumers; i++)
		pthread_join(threads[i], NULL);

	char detail[128];
	snprintf(detail, sizeof(detail), "%d blocks freed by other threads, %d "
		"damaged", (int)sConsumed, (int)sBadBlocks);
	check(sBadBlocks == 0 && sConsumed == kProducers * 20000, "cross-thread",
		detail);
}


// #pragma mark - thread churn


#define SURVIVORS_PER_THREAD 50
#define CHURN_THREADS 320

static void* sSurvivors[CHURN_THREADS * SURVIVORS_PER_THREAD];
static size_t sSurvivorSizes[CHURN_THREADS * SURVIVORS_PER_THREAD];


static void*
churner(void* data)
{
	int index = (int)(intptr_t)data;
	uint64_t state = index * 7919 + 17;
	void* blocks[200];
	size_t sizes[200];
	for (int i = 0; i < 200; i++) {
		sizes[i] = 16 + random64(&state) % 3000;
		blocks[i] = malloc(sizes[i]);
		fill(blocks[i], sizes[i], index * 1000 + i);
	}
	for (int i = 0; i < 200; i++) {
		if (i < SURVIVORS_PER_THREAD) {
			int slot = index * SURVIVORS_PER_THREAD + i;
			sSurvivors[slot] = blocks[i];
			sSurvivorSizes[slot] = sizes[i];
		} else
			free(blocks[i]);
	}
	return NULL;
}


static void
test_thread_churn()
{
	for (int wave = 0; wave < CHURN_THREADS; wave += 16) {
		pthread_t threads[16];
		for (int i = 0; i < 16; i++) {
			pthread_create(&threads[i], NULL, churner,
				(void*)(intptr_t)(wave + i));
		}
		for (int i = 0; i < 16; i++)
			pthread_join(threads[i], NULL);
	}

	int damaged = 0;
	for (int t = 0; t < CHURN_THREADS; t++) {
		for (int i = 0; i < SURVIVORS_PER_THREAD; i++) {
			int slot = t * SURVIVORS_PER_THREAD + i;
			if (!verify(sSurvivors[slot], sSurvivorSizes[slot],
					t * 1000 + i))
				damaged++;
		}
	}
	// the exited threads' memory must be usable by this one
	void* again[4000];
	for (int i = 0; i < 4000; i++) {
		again[i] = malloc(16 + i % 3000);
		memset(again[i], 0x77, 16 + i % 3000);
	}
	for (int t = 0; t < CHURN_THREADS; t++) {
		for (int i = 0; i < SURVIVORS_PER_THREAD; i++) {
			int slot = t * SURVIVORS_PER_THREAD + i;
			if (!verify(sSurvivors[slot], sSurvivorSizes[slot],
					t * 1000 + i))
				damaged++;
			free(sSurvivors[slot]);
		}
	}
	for (int i = 0; i < 4000; i++)
		free(again[i]);

	char detail[128];
	snprintf(detail, sizeof(detail), "%d threads exited leaving %d blocks; "
		"%d damaged", CHURN_THREADS, CHURN_THREADS * SURVIVORS_PER_THREAD,
		damaged);
	check(damaged == 0, "thread churn", detail);
}


// #pragma mark - fork


static volatile bool sStopBackground = false;


static void*
background_allocator(void* data)
{
	uint64_t state = (uintptr_t)data + 99;
	void* slots[64] = {};
	while (!sStopBackground) {
		int i = random64(&state) % 64;
		free(slots[i]);
		slots[i] = malloc(16 + random64(&state) % 5000);
	}
	for (int i = 0; i < 64; i++)
		free(slots[i]);
	return NULL;
}


static void
test_fork()
{
	pthread_t threads[4];
	sStopBackground = false;
	for (int i = 0; i < 4; i++)
		pthread_create(&threads[i], NULL, background_allocator,
			(void*)(intptr_t)i);

	int good = 0;
	int forks = 20;
	for (int f = 0; f < forks; f++) {
		pid_t child = fork();
		if (child == 0) {
			uint64_t state = f + 5;
			void* blocks[500];
			size_t sizes[500];
			for (int round = 0; round < 4; round++) {
				for (int i = 0; i < 500; i++) {
					sizes[i] = 1 + random64(&state) % 20000;
					blocks[i] = malloc(sizes[i]);
					fill(blocks[i], sizes[i], i);
				}
				for (int i = 0; i < 500; i++) {
					if (!verify(blocks[i], sizes[i], i))
						_exit(2);
					free(blocks[i]);
				}
			}
			_exit(0);
		}
		int status = 0;
		if (child > 0 && waitpid(child, &status, 0) == child
			&& WIFEXITED(status) && WEXITSTATUS(status) == 0)
			good++;
		snooze(2000);
	}

	sStopBackground = true;
	for (int i = 0; i < 4; i++)
		pthread_join(threads[i], NULL);

	char detail[128];
	snprintf(detail, sizeof(detail), "%d of %d children, forked while four "
		"threads allocated, allocated and exited cleanly", good, forks);
	check(good == forks, "fork", detail);
}


// #pragma mark - memory given back


static void
test_memory_return()
{
	system_info info;
	get_system_info(&info);
	uint64_t ram = (uint64_t)info.max_pages * B_PAGE_SIZE;

	int32 areasBefore;
	uint64_t before = team_resident(&areasBefore);

	// 1 GB in 4 MB blocks, every page touched
	enum { kBlocks = 256 };
	const size_t kBlock = 4 * 1024 * 1024;
	void* blocks[kBlocks];
	bool allocated = true;
	for (int i = 0; i < kBlocks; i++) {
		blocks[i] = malloc(kBlock);
		if (blocks[i] == NULL) {
			allocated = false;
			break;
		}
		for (size_t offset = 0; offset < kBlock; offset += 4096)
			((uint8_t*)blocks[i])[offset] = 1;
	}
	uint64_t peak = team_resident(NULL);
	for (int i = 0; i < kBlocks; i++)
		free(blocks[i]);

	int32 areasAfter;
	uint64_t after = team_resident(&areasAfter);
	uint64_t allowed = ram / 64 + 32 * 1024 * 1024;

	char detail[200];
	snprintf(detail, sizeof(detail), "resident %llu MB before, %llu MB with "
		"1 GB touched, %llu MB after freeing it (at most %llu MB more "
		"allowed); areas %d -> %d",
		(unsigned long long)(before >> 20), (unsigned long long)(peak >> 20),
		(unsigned long long)(after >> 20), (unsigned long long)(allowed >> 20),
		(int)areasBefore, (int)areasAfter);
	check(allocated && after <= before + allowed, "memory return", detail);
}


// #pragma mark - many small blocks


static void
test_many_small()
{
	enum { kCount = 1000000 };
	void** blocks = malloc(kCount * sizeof(void*));
	bool ok = blocks != NULL;
	for (int i = 0; i < kCount && ok; i++) {
		blocks[i] = malloc(24 + (i & 7) * 4);
		if (blocks[i] == NULL)
			ok = false;
		else
			*(uint32_t*)blocks[i] = (uint32_t)i;
	}
	for (int i = 0; i < kCount && ok; i++) {
		if (*(uint32_t*)blocks[i] != (uint32_t)i)
			ok = false;
	}
	uint64_t state = 3;
	if (ok)
		shuffle(blocks, kCount, &state);
	for (int i = 0; i < kCount && blocks != NULL; i++)
		free(blocks[i]);
	free(blocks);
	check(ok, "many small", "a million blocks of 24-52 bytes live at once");
}


// #pragma mark - double free


static void
test_double_free()
{
	pid_t child = fork();
	if (child == 0) {
		// through a volatile, so the compiler lets the mistake be made
		void* volatile block = malloc(48);
		free(block);
		free(block);
		_exit(0);
	}
	int status = 0;
	waitpid(child, &status, 0);
	bool caught = !WIFEXITED(status) || WEXITSTATUS(status) != 0;
	check(caught, "double free", caught ? "the second free stopped the team"
		: "a double free went unnoticed");
}


int
main(int argc, char** argv)
{
	bool crashTests = argc > 1 && strcmp(argv[1], "--crash") == 0;
	bigtime_t start = system_time();

	test_sizes();
	test_calloc();
	test_realloc();
	test_alignment();
	test_cross_thread();
	test_thread_churn();
	test_fork();
	test_many_small();
	test_memory_return();
	if (crashTests)
		test_double_free();

	int32 areas;
	team_resident(&areas);
	printf("(%.2f s, %d areas at the end)\n",
		(system_time() - start) / 1e6, (int)areas);
	printf("SELFTEST %s %d/%d\n", sPassed == sTotal ? "PASS" : "FAIL",
		sPassed, sTotal);
	return sPassed == sTotal ? 0 : 1;
}
