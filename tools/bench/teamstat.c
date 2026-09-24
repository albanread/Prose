/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	teamstat: run a command and report what it used.

	Samples the command's team every interval: its user and kernel time, and
	each thread's; its memory, from its areas (resident and reserved); its
	thread count; and the system's page-fault count. At the end: the totals,
	the peaks, and the threads that spent the most time in the kernel.

		teamstat [-i <ms>] [-v] <command> [<argument>...]

	-v prints a line a sample as it runs.
*/

#include <OS.h>

#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


extern char** environ;


#define MAX_THREADS 256


typedef struct {
	thread_id	id;
	char		name[B_OS_NAME_LENGTH];
	bigtime_t	user;
	bigtime_t	kernel;
} thread_record;


static thread_record sThreads[MAX_THREADS];
static int sThreadCount = 0;


static void
record_thread(const thread_info* info)
{
	for (int i = 0; i < sThreadCount; i++) {
		if (sThreads[i].id == info->thread) {
			sThreads[i].user = info->user_time;
			sThreads[i].kernel = info->kernel_time;
			return;
		}
	}
	if (sThreadCount == MAX_THREADS)
		return;
	thread_record* record = &sThreads[sThreadCount++];
	record->id = info->thread;
	strlcpy(record->name, info->name, sizeof(record->name));
	record->user = info->user_time;
	record->kernel = info->kernel_time;
}


static int
compare_kernel(const void* a, const void* b)
{
	bigtime_t ka = ((const thread_record*)a)->kernel;
	bigtime_t kb = ((const thread_record*)b)->kernel;
	return ka < kb ? 1 : ka > kb ? -1 : 0;
}


int
main(int argc, char** argv)
{
	bigtime_t interval = 20000;
	bool verbose = false;
	int first = 1;
	while (first < argc && argv[first][0] == '-') {
		if (strcmp(argv[first], "-i") == 0 && first + 1 < argc) {
			interval = atoll(argv[first + 1]) * 1000;
			first += 2;
		} else if (strcmp(argv[first], "-v") == 0) {
			verbose = true;
			first++;
		} else
			break;
	}
	if (first >= argc) {
		fprintf(stderr, "usage: teamstat [-i <ms>] [-v] <command> [<arg>...]\n");
		return 1;
	}

	system_info before;
	get_system_info(&before);
	bigtime_t start = system_time();

	pid_t child;
	int error = posix_spawnp(&child, argv[first], NULL, NULL, argv + first,
		environ);
	if (error != 0) {
		fprintf(stderr, "teamstat: cannot run %s: %s\n", argv[first],
			strerror(error));
		return 1;
	}

	uint64 peakResident = 0;
	uint64 peakReserved = 0;
	int32 peakAreas = 0;
	int32 peakThreads = 0;
	int samples = 0;
	int status = 0;
	while (true) {
		if (waitpid(child, &status, WNOHANG) == child)
			break;

		uint64 resident = 0;
		uint64 reserved = 0;
		int32 areas = 0;
		ssize_t areaCookie = 0;
		area_info area;
		while (get_next_area_info(child, &areaCookie, &area) == B_OK) {
			resident += area.ram_size;
			reserved += area.size;
			areas++;
		}
		if (resident > peakResident)
			peakResident = resident;
		if (reserved > peakReserved)
			peakReserved = reserved;
		if (areas > peakAreas)
			peakAreas = areas;

		int32 threads = 0;
		int32 threadCookie = 0;
		thread_info thread;
		while (get_next_thread_info(child, &threadCookie, &thread) == B_OK) {
			record_thread(&thread);
			threads++;
		}
		if (threads > peakThreads)
			peakThreads = threads;

		if (verbose) {
			team_usage_info usage;
			system_info now;
			if (get_team_usage_info(child, B_TEAM_USAGE_SELF, &usage) == B_OK
				&& get_system_info(&now) == B_OK) {
				printf("%7.3f s  user %7.3f  kernel %7.3f  resident %6llu MB"
					"  areas %5d  threads %3d  faults %u\n",
					(system_time() - start) / 1e6, usage.user_time / 1e6,
					usage.kernel_time / 1e6,
					(unsigned long long)(resident >> 20), (int)areas,
					(int)threads, now.page_faults - before.page_faults);
			}
		}
		samples++;
		snooze(interval);
	}

	bigtime_t end = system_time();
	system_info after;
	get_system_info(&after);
	team_usage_info usage;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_CHILDREN, &usage);

	uint32 faults = after.page_faults - before.page_faults;
	double wall = (end - start) / 1e6;
	printf("wall %.3f s, user %.3f s, kernel %.3f s (%.0f%% of CPU time)\n",
		wall, usage.user_time / 1e6, usage.kernel_time / 1e6,
		100.0 * usage.kernel_time / (usage.user_time + usage.kernel_time));
	printf("page faults %u (system-wide), %.0f a second, %.2f us of kernel "
		"time each if they were all of it\n", faults, faults / wall,
		faults > 0 ? usage.kernel_time / (double)faults : 0.0);
	printf("peak resident %llu MB, peak reserved %llu MB, peak areas %d, "
		"peak threads %d (%d samples)\n",
		(unsigned long long)(peakResident >> 20),
		(unsigned long long)(peakReserved >> 20), (int)peakAreas,
		(int)peakThreads, samples);
	printf("system pages in use: %llu MB before, %llu MB after\n",
		(unsigned long long)(before.used_pages * B_PAGE_SIZE >> 20),
		(unsigned long long)(after.used_pages * B_PAGE_SIZE >> 20));

	qsort(sThreads, sThreadCount, sizeof(sThreads[0]), compare_kernel);
	printf("threads by kernel time (as last sampled):\n");
	for (int i = 0; i < sThreadCount && i < 10; i++) {
		printf("  %-32s user %7.3f s  kernel %7.3f s\n", sThreads[i].name,
			sThreads[i].user / 1e6, sThreads[i].kernel / 1e6);
	}
	printf("exit status %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
