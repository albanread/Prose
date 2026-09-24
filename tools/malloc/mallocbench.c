/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	mallocbench: allocator throughput with 1 to n threads.

		mallocbench [-t <threads>] [-n <operations per thread>] [pattern...]

	Patterns (all, if none is named):
		local	each thread churns its own working set of 4096 blocks, sizes
				weighted to the small ones as a compiler's are
		remote	half the threads allocate, in batches of 256, and hand the
				batches to the other half, which free them
		large	64 KB to 16 MB buffers, every page touched, then freed
		realloc	vectors grown by half again, from 16 bytes to 4 MB

	For each: wall time, operations a second (all threads together: a
	malloc and its free in local and remote, one buffer in large, one grown
	vector in realloc), the team's user and kernel time, page faults
	(system-wide) and the team's areas at the end.
*/

#include <OS.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int sThreads = 1;
static long sOperations = 2000000;


static uint64_t
random64(uint64_t* state)
{
	uint64_t x = *state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * 0x2545f4914f6cdd1dULL;
}


/*!	Sizes as a compiler asks for them: mostly under 128 bytes, some to 1 KB,
	a few to 8 KB and the odd one to 64 KB. */
static size_t
compiler_size(uint64_t* state)
{
	uint64_t r = random64(state);
	uint32_t pick = r % 1000;
	r >>= 10;
	if (pick < 800)
		return 8 + r % 120;
	if (pick < 950)
		return 128 + r % 900;
	if (pick < 990)
		return 1024 + r % 7168;
	return 8192 + r % 57344;
}


// #pragma mark - local


static void*
local_thread(void* data)
{
	uint64_t state = (uintptr_t)data * 0x9e3779b97f4a7c15ULL + 1;
	enum { kSlots = 4096 };
	void** slots = calloc(kSlots, sizeof(void*));
	for (long i = 0; i < sOperations; i++) {
		uint32_t slot = random64(&state) % kSlots;
		free(slots[slot]);
		size_t size = compiler_size(&state);
		char* block = malloc(size);
		block[0] = (char)i;
		block[size - 1] = (char)i;
		slots[slot] = block;
	}
	for (int i = 0; i < kSlots; i++)
		free(slots[i]);
	free(slots);
	return NULL;
}


// #pragma mark - remote


#define BATCH 256
#define QUEUE_DEPTH 8

typedef struct {
	pthread_mutex_t	lock;
	pthread_cond_t	changed;
	void**			batches[QUEUE_DEPTH];
	int				head;
	int				count;
	bool			done;
} handoff;


static handoff* sHandoffs;


static void*
producer_thread(void* data)
{
	handoff* h = &sHandoffs[(intptr_t)data];
	uint64_t state = (uintptr_t)data * 31 + 7;
	for (long i = 0; i < sOperations; i += BATCH) {
		void** batch = malloc(BATCH * sizeof(void*));
		for (int j = 0; j < BATCH; j++) {
			size_t size = compiler_size(&state);
			char* block = malloc(size);
			block[0] = 1;
			batch[j] = block;
		}
		pthread_mutex_lock(&h->lock);
		while (h->count == QUEUE_DEPTH)
			pthread_cond_wait(&h->changed, &h->lock);
		h->batches[(h->head + h->count) % QUEUE_DEPTH] = batch;
		h->count++;
		pthread_cond_broadcast(&h->changed);
		pthread_mutex_unlock(&h->lock);
	}
	pthread_mutex_lock(&h->lock);
	h->done = true;
	pthread_cond_broadcast(&h->changed);
	pthread_mutex_unlock(&h->lock);
	return NULL;
}


static void*
consumer_thread(void* data)
{
	handoff* h = &sHandoffs[(intptr_t)data];
	while (true) {
		pthread_mutex_lock(&h->lock);
		while (h->count == 0 && !h->done)
			pthread_cond_wait(&h->changed, &h->lock);
		if (h->count == 0) {
			pthread_mutex_unlock(&h->lock);
			break;
		}
		void** batch = h->batches[h->head];
		h->head = (h->head + 1) % QUEUE_DEPTH;
		h->count--;
		pthread_cond_broadcast(&h->changed);
		pthread_mutex_unlock(&h->lock);

		for (int j = 0; j < BATCH; j++)
			free(batch[j]);
		free(batch);
	}
	return NULL;
}


// #pragma mark - large


static void* volatile sSink;


static void*
large_thread(void* data)
{
	uint64_t state = (uintptr_t)data * 131 + 3;
	long rounds = sOperations / 1000;
	if (rounds < 50)
		rounds = 50;
	for (long i = 0; i < rounds; i++) {
		// log-uniform from 64 KB to 16 MB
		int shift = 16 + random64(&state) % 8;
		size_t size = ((size_t)1 << shift) + random64(&state) % ((size_t)1
			<< shift);
		char* block = malloc(size);
		for (size_t offset = 0; offset < size; offset += 4096)
			block[offset] = (char)offset;
		sSink = block;
		free(block);
	}
	return NULL;
}


// #pragma mark - realloc


static void*
realloc_thread(void* data)
{
	(void)data;
	long rounds = sOperations / 20000;
	if (rounds < 20)
		rounds = 20;
	for (long i = 0; i < rounds; i++) {
		char* vector = NULL;
		size_t length = 0;
		for (size_t size = 16; size <= 4 * 1024 * 1024; size += size / 2) {
			vector = realloc(vector, size);
			memset(vector + length, (int)size, size - length);
			length = size;
		}
		free(vector);
	}
	return NULL;
}


// #pragma mark -


static void
run(const char* name, void* (*function)(void*), bool paired)
{
	system_info before;
	get_system_info(&before);
	team_usage_info usageBefore;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &usageBefore);
	bigtime_t start = system_time();

	int pairs = sThreads / 2 > 0 ? sThreads / 2 : 1;
	int count = paired ? pairs * 2 : sThreads;
	pthread_t* threads = malloc(count * sizeof(pthread_t));
	if (paired) {
		sHandoffs = calloc(pairs, sizeof(handoff));
		for (int i = 0; i < pairs; i++) {
			pthread_mutex_init(&sHandoffs[i].lock, NULL);
			pthread_cond_init(&sHandoffs[i].changed, NULL);
		}
		for (int i = 0; i < pairs; i++) {
			pthread_create(&threads[2 * i], NULL, producer_thread,
				(void*)(intptr_t)i);
			pthread_create(&threads[2 * i + 1], NULL, consumer_thread,
				(void*)(intptr_t)i);
		}
	} else {
		for (int i = 0; i < count; i++)
			pthread_create(&threads[i], NULL, function, (void*)(intptr_t)i);
	}
	for (int i = 0; i < count; i++)
		pthread_join(threads[i], NULL);
	free(threads);
	if (paired) {
		free(sHandoffs);
		sHandoffs = NULL;
	}

	bigtime_t wall = system_time() - start;
	team_usage_info usage;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &usage);
	system_info after;
	get_system_info(&after);
	int32 areas = 0;
	ssize_t cookie = 0;
	area_info area;
	while (get_next_area_info(B_CURRENT_TEAM, &cookie, &area) == B_OK)
		areas++;

	double operations = (double)sOperations * sThreads;
	if (paired)
		operations = (double)sOperations * pairs;
	else if (function == large_thread)
		operations = sThreads * (double)(sOperations / 1000 < 50 ? 50
			: sOperations / 1000);
	else if (function == realloc_thread)
		operations = sThreads * (double)(sOperations / 20000 < 20 ? 20
			: sOperations / 20000);
	printf("%-8s %2d thr  %9.1f ms  %10.0f ops/s  user %6.2f s  kernel %6.2f s"
		"  faults %7u  areas %4d\n", name, sThreads, wall / 1000.0,
		operations * 1e6 / wall,
		(usage.user_time - usageBefore.user_time) / 1e6,
		(usage.kernel_time - usageBefore.kernel_time) / 1e6,
		after.page_faults - before.page_faults, (int)areas);
	fflush(stdout);
}


int
main(int argc, char** argv)
{
	int first = 1;
	while (first < argc && argv[first][0] == '-') {
		if (strcmp(argv[first], "-t") == 0 && first + 1 < argc) {
			sThreads = atoi(argv[first + 1]);
			first += 2;
		} else if (strcmp(argv[first], "-n") == 0 && first + 1 < argc) {
			sOperations = atol(argv[first + 1]);
			first += 2;
		} else {
			fprintf(stderr, "usage: mallocbench [-t <threads>] "
				"[-n <operations>] [local|remote|large|realloc...]\n");
			return 1;
		}
	}

	bool all = first >= argc;
	for (int i = all ? 0 : first; all ? i < 4 : i < argc; i++) {
		const char* pattern = all
			? (const char*[]){ "local", "remote", "large", "realloc" }[i]
			: argv[i];
		if (strcmp(pattern, "local") == 0)
			run("local", local_thread, false);
		else if (strcmp(pattern, "remote") == 0)
			run("remote", NULL, true);
		else if (strcmp(pattern, "large") == 0)
			run("large", large_thread, false);
		else if (strcmp(pattern, "realloc") == 0)
			run("realloc", realloc_thread, false);
		else
			fprintf(stderr, "mallocbench: no pattern '%s'\n", pattern);
	}
	return 0;
}
