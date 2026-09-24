/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	A program whose profile is known: three quarters of its user time in
	hog_three_quarters(), a quarter in hog_one_quarter(), both reached
	through hog_caller(); and a stretch of syscalls in hog_syscalls(). The
	profiler's regression test checks that `profile` says so.
*/

#include <OS.h>
#include <stdio.h>
#include <unistd.h>


static volatile unsigned long sSink;


__attribute__((noinline)) void
hog_three_quarters(bigtime_t duration)
{
	bigtime_t end = system_time() + duration;
	unsigned long x = 1;
	while (system_time() < end) {
		for (int i = 0; i < 10000; i++)
			x = x * 6364136223846793005UL + 1442695040888963407UL;
	}
	sSink = x;
}


__attribute__((noinline)) void
hog_one_quarter(bigtime_t duration)
{
	bigtime_t end = system_time() + duration;
	unsigned long x = 2;
	while (system_time() < end) {
		for (int i = 0; i < 10000; i++)
			x = x * 2862933555777941757UL + 3037000493UL;
	}
	sSink = x;
}


__attribute__((noinline)) void
hog_caller(void)
{
	for (int i = 0; i < 20; i++) {
		hog_three_quarters(75000);
		hog_one_quarter(25000);
	}
}


__attribute__((noinline)) void
hog_syscalls(bigtime_t duration)
{
	bigtime_t end = system_time() + duration;
	while (system_time() < end) {
		thread_info info;
		get_thread_info(find_thread(NULL), &info);
	}
}


int
main(void)
{
	hog_caller();
	hog_syscalls(1000000);
	printf("done\n");
	return 0;
}
