# Profiler test

The regression test for Haiku patches 0136 and 0137: the profilers work on
arm64.

- **0136:** `arch_get_stack_trace()` and `arch_debug_get_interrupt_pc()` were
  stubs on arm64, so `profile` attributed every tick to the commpage and
  `profile -a` counted every tick as unknown. They walk frame records and
  iframes now, as on x86.
- **0137:** the arm64 syscall stubs had no symbol size, so a sample in one --
  where every thread in a syscall is -- matched no function and was charged
  to its caller's caller.

`cpuhog` spends three quarters of a busy stretch in `hog_three_quarters()`
and a quarter in `hog_one_quarter()`, then a second in `get_thread_info()`
syscalls. On Prose:

	sh build-and-run.sh

Five checks, each PASS or FAIL, then `SELFTEST PASS 5/5`: no unknown ticks,
the two hot functions 3:1, the syscall second in `_kern_get_thread_info`,
and with `-k` kernel hits, including the syscall's `_user_get_thread_info`.
