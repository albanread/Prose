// Kernel contention: every operation here is a syscall that takes kernel
// spinlocks and does kernel atomics, so it measures the kernel's code, not this
// program's. Two loads, each for 3 s across N threads:
//   create: create_sem + delete_sem  (the global semaphore table)
//   shared: acquire_sem + release_sem on one semaphore (one hot lock)
//
// Throughput alone can flatter an unfair lock: one thread winning it again and
// again from its own cache does a great deal of work while the rest starve. So
// each load also reports the spread between the busiest and idlest thread.
#include <OS.h>
#include <stdint.h>
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

struct result {
	double	perSecond;
	uint64	least;
	uint64	most;
};


static struct result
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

	struct result out = { 0, UINT64_MAX, 0 };
	uint64 total = 0;
	for (int i = 0; i < threads; i++) {
		status_t result;
		wait_for_thread(ids[i], &result);
		total += counts[i];
		if (counts[i] < out.least)
			out.least = counts[i];
		if (counts[i] > out.most)
			out.most = counts[i];
	}
	out.perSecond = total / 3.0;
	return out;
}


static void
report(const char* name, struct result r)
{
	printf(" %s %.0f ops/s (thread spread %.1fx)", name, r.perSecond,
		r.least > 0 ? (double)r.most / r.least : 0.0);
}

int
main(int argc, char** argv)
{
	int threads = argc > 1 ? atoi(argv[1]) : 14;
	sShared = create_sem(1, "shared");
	for (int round = 1; round <= 3; round++) {
		printf("round %d, %d threads:", round, threads);
		report("create", run(create_loop, threads));
		report(" shared", run(shared_loop, threads));
		printf("\n");
	}
	return 0;
}
