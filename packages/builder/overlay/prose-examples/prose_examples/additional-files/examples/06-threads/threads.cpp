/*
 * Threads, and keeping them out of each other's way.
 *
 *     clang++ -O2 -Wall -o threads threads.cpp -lbe
 *
 * Threads here are made with spawn_thread() and start when they are resumed.
 * wait_for_thread() waits for one to end and collects what it returned.
 * Two threads that touch the same thing need a lock between them: a BLocker
 * is the usual one, and LockAutoLocker-style scoping keeps it simple.
 */

#include <Locker.h>
#include <OS.h>

#include <stdio.h>
#include <string.h>

struct Shared {
	BLocker	lock;
	int64	total;

	Shared()
		:
		lock("counter"),
		total(0)
	{
	}
};

const int kThreads = 4;
const int kStepsEach = 50000;

static status_t
count_up(void* data)
{
	Shared* shared = (Shared*)data;

	for (int i = 0; i < kStepsEach; i++) {
		// held for as short a time as possible: everything else waits
		shared->lock.Lock();
		shared->total++;
		shared->lock.Unlock();
	}

	return B_OK;
}

int
main()
{
	Shared shared;
	thread_id threads[kThreads];

	bigtime_t started = system_time();

	for (int i = 0; i < kThreads; i++) {
		char name[32];
		snprintf(name, sizeof(name), "counter %d", i + 1);

		threads[i] = spawn_thread(count_up, name, B_NORMAL_PRIORITY, &shared);
		if (threads[i] < B_OK) {
			fprintf(stderr, "could not make a thread: %s\n", strerror(threads[i]));
			return 1;
		}

		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreads; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
	}

	bigtime_t took = system_time() - started;

	printf("%d threads counted to %" B_PRId64 " in %" B_PRId64 " ms\n",
		kThreads, shared.total, took / 1000);

	if (shared.total == (int64)kThreads * kStepsEach) {
		printf("the count is exact: the lock did its job\n");
		return 0;
	}

	printf("the count is wrong, which is what a missing lock looks like\n");
	return 1;
}
