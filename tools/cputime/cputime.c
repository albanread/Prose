/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	Regression test for Haiku patch 0135: a thread's time in user mode is
	reported as user time. Before it, arm64 charged nearly all computation
	as kernel time -- 0.004 s of user time for 2 s of arithmetic.

	Each phase runs for a second and is judged by the team's own usage.
*/

#include <OS.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int sPassed = 0;
static int sFailed = 0;
static volatile unsigned long sSink;


static void
check(bool ok, const char* name, bigtime_t user, bigtime_t kernel,
	bigtime_t wall)
{
	printf("%s %s: user %.3f s, kernel %.3f s, wall %.3f s\n",
		ok ? "PASS" : "FAIL", name, user / 1e6, kernel / 1e6, wall / 1e6);
	if (ok)
		sPassed++;
	else
		sFailed++;
}


typedef void (*phase_function)(bigtime_t end);


static void
arithmetic(bigtime_t end)
{
	unsigned long x = 1;
	while (system_time() < end) {
		for (int i = 0; i < 10000; i++)
			x = x * 6364136223846793005UL + 1442695040888963407UL;
	}
	sSink = x;
}


static char* sBuffer;
static const size_t kBufferSize = 16 << 20;


static void
reading(bigtime_t end)
{
	unsigned long sum = 0;
	while (system_time() < end) {
		for (size_t i = 0; i < kBufferSize; i += 64)
			sum += sBuffer[i];
	}
	sSink = sum;
}


static void
syscalls(bigtime_t end)
{
	thread_id self = find_thread(NULL);
	while (system_time() < end) {
		thread_info info;
		get_thread_info(self, &info);
	}
}


static void
run(const char* name, phase_function function, bool expectUser)
{
	team_usage_info before;
	team_usage_info after;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &before);
	bigtime_t start = system_time();
	function(start + 1000000);
	bigtime_t wall = system_time() - start;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &after);

	bigtime_t user = after.user_time - before.user_time;
	bigtime_t kernel = after.kernel_time - before.kernel_time;
	bigtime_t total = user + kernel;
	// One busy thread: its user and kernel time together are its wall time,
	// give or take what other threads took from its CPU.
	bool whole = total > wall * 8 / 10 && total < wall * 11 / 10;
	bool split = expectUser ? user >= total * 95 / 100
		: kernel >= total / 2;
	check(whole && split, name, user, kernel, wall);
}


int
main()
{
	sBuffer = malloc(kBufferSize);
	memset(sBuffer, 1, kBufferSize);

	run("arithmetic is user time", arithmetic, true);
	run("reading mapped memory is user time", reading, true);
	run("a syscall loop is mostly kernel time", syscalls, false);

	printf("SELFTEST %s %d/%d\n", sFailed == 0 ? "PASS" : "FAIL", sPassed,
		sPassed + sFailed);
	return sFailed == 0 ? 0 : 1;
}
