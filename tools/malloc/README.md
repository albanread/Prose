# malloc test and benchmark

The regression test and the benchmark for Haiku patch 0138, which gave
libroot's allocator thread caches, a page owner map, 16 KB allocator pages
and one overcommitting heap area (design: `docs/malloc.md`).

On Prose:

	sh build-and-run.sh            # or: sh build-and-run.sh --crash

`malloctest` checks, each PASS or FAIL, ending `SELFTEST PASS 10/10`
(9 without `--crash`):

- **sizes**: every size class and region size, 0 bytes to 64 MB, live at
  once, filled with a pattern and verified. It also checks their alignment
  and `malloc_usable_size()`.
- **calloc**: blocks are dirtied and freed, and `calloc()` of the same sizes
  must return zeroes; an overflowing `calloc()` fails with `ENOMEM`.
- **realloc**: contents are kept across every pair of sizes from 1 byte to
  5 MB, growing and shrinking, and in a vector grown by half again to 8 MB.
- **alignment**: `posix_memalign()`, `memalign()`, `aligned_alloc()` and
  `valloc()`, alignments up to 1 MB.
- **cross-thread**: 4 threads allocate and hand the blocks to 4 others,
  which verify and free them.
- **thread churn**: 320 threads exit, leaving blocks behind for the main
  thread to verify and free.
- **fork**: 20 children, forked while 4 threads allocate, allocate and exit
  cleanly.
- **many small**: a million small blocks live at once.
- **memory return**: after 1 GB is touched and freed, the team's resident
  memory must come back to within 1/64 of the machine's memory (plus 32 MB)
  of where it was.
- **double free** (`--crash`): the second `free()` must stop the team.
  This needs the debug_server to kill crashing teams (`default_action
  kill`), or it waits at an alert.

`mallocbench [-t threads] [-n operations] [local|remote|large|realloc]`
measures throughput over four patterns:

- **local**: churn of a compiler's sizes in each thread's own working set.
- **remote**: blocks allocated in one thread and freed in another.
- **large**: 64 KB to 16 MB buffers, every page touched.
- **realloc**: vectors grown to 4 MB.

It reports operations a second, user and kernel time, page faults and the
team's areas.

A test libroot can be tried without a new image: put it in `lib/` next to
the programs. The loader looks in the program's own `lib/` directory
(`%A/lib`) first.
