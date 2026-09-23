// Kernel contention: every operation here is a syscall that takes kernel
// spinlocks and does kernel atomics, so it measures the kernel's code, not this
// program's. Two loads, each for 3 s across N threads:
//   create: create_sem + delete_sem  (the global semaphore table)
//   shared: acquire_sem + release_sem on one semaphore (one hot lock)
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int sStop;
static sem_id sShared;

static int32
create_loop(void* data)
{
	uint64* count = (uint64*)data;
	while (!sStop) {
		sem_id sem = create_sem(0, "bench");
		delete_sem(sem);
		(*count)++;
	}
	return 0;
}

static int32
shared_loop(void* data)
{
	uint64* count = (uint64*)data;
	while (!sStop) {
		acquire_sem(sShared);
		release_sem(sShared);
		(*count)++;
	}
	return 0;
}

static double
run(thread_func loop, int threads)
{
	uint64 counts[64] = { 0 };
	thread_id ids[64];
	sStop = 0;
	for (int i = 0; i < threads; i++) {
		ids[i] = spawn_thread(loop, "bench", B_NORMAL_PRIORITY, &counts[i]);
		resume_thread(ids[i]);
	}
	snooze(3000000);
	sStop = 1;
	uint64 total = 0;
	for (int i = 0; i < threads; i++) {
		status_t result;
		wait_for_thread(ids[i], &result);
		total += counts[i];
	}
	return total / 3.0;
}

int
main(int argc, char** argv)
{
	int threads = argc > 1 ? atoi(argv[1]) : 14;
	sShared = create_sem(1, "shared");
	for (int round = 1; round <= 3; round++) {
		double create = run(create_loop, threads);
		double shared = run(shared_loop, threads);
		printf("round %d, %d threads: create %.0f ops/s, shared %.0f ops/s\n", round, threads,
			create, shared);
	}
	return 0;
}
