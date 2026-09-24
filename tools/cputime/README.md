# CPU time test

The regression test for Haiku patch 0135: time a thread spends in user mode
is reported as user time. Before 0135, arm64 recorded a thread's entry into
the kernel only for syscalls; an interrupt or a fault from user mode came
back through `thread_at_kernel_exit()`, which charged everything since the
last recorded crossing to kernel time. Two seconds of pure arithmetic read
0.004 s user and 1.996 s kernel, and every `time`, `top` and team-usage
figure on Prose was wrong the same way (it made the Mojo compiler look as
if it spent three quarters of its time in the kernel).

On Prose:

	sh build-and-run.sh

Three one-second phases, each judged by the team's own usage: arithmetic
and reading mapped memory must be at least 95% user time, a loop of
syscalls at least half kernel time, and in each the user and kernel time
together must come to the wall time (one busy thread). `SELFTEST PASS 3/3`
on the image with 0135.
