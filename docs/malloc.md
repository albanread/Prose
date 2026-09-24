# malloc for Prose: design

*2026-09-24. Why libroot's allocator is slow on a multi-core Prose machine, what
Linux and Darwin do instead, and the change we are making.*

## Why

The Mojo compiler on Prose (`stdlib_check.mojo`, 8 CPUs) ran **60% slower with
8 threads than with one**: 6.1 s against 3.8 s wall time, with 2.2 times the CPU
time. The profile, taken once patches 0135–0137 had made the profiler and the
CPU-time accounting trustworthy on arm64:

| where the CPU time went (8 threads)                    | share |
|--------------------------------------------------------|------:|
| `malloc` + `free`, inclusive                           | 36%   |
| of which, sleeping and waking on contended pool locks  | ~16%  |
| page faults (`vm_page_fault`)                          | 7.5%  |

One compile also made 11,855 unmaps and 9,247 `resize_area` calls, and left the
compiler with 2,414 "heap area" areas, most of them 4–32 KB. The allocator
serialises the threads, and it keeps asking the kernel to reshape the heap.

## What Haiku has now

libroot's malloc is OpenBSD's (`malloc.c` 1.296) over a Haiku layer,
`PagesAllocator`. Upstream switched to it in 2025. The rpmalloc attempt of
2019 had fallen over on Medo's 32 MB video frames. mimalloc was turned down for
three reasons: it had little history as a libc allocator; it commits memory
with `mprotect`, which gives a Haiku area per-page protection arrays, so it
costs kernel memory and slows fork, faults and unmapping; and it reserves
about 1 GB per thread, which crashed app_server
([forum thread](https://discuss.haiku-os.org/t/a-new-malloc-for-haiku-userland-based-around-openbsds-malloc/16377)).

How it works:

- **Pools.** There are 8 pools (7 used); a thread is assigned one by its
  malloc id. Each pool has a plain mutex. Haiku's userland mutex does not
  spin: a contended lock is a syscall to sleep and another to wake.
- **Objects up to 2 KB** are carved from 4 KB pages, with a bitmap per page.
  Larger ones are whole pages, with each region recorded in its pool's
  hash table.
- **`free()` locks the freeing thread's own pool first.** If the pointer
  belongs to another pool (another thread allocated it), `findpool()`
  unlocks and locks the other pools one at a time until it finds the
  owner. That is the lock traffic in the profile.
- **Pages** come from `PagesAllocator`: areas inside a 1 GB reservation,
  grown by `resize_area` one allocation at a time and committed in full.
  Freed pages beyond 25% of the heap (at least 512 KB, at most 128 MB) are
  unmapped. Unmapping the middle of an area splits it in two, which is where
  the thousands of small areas come from.

## What Linux does

**glibc** ([malloc.c](https://github.com/bminor/glibc/blob/master/malloc/malloc.c)):

- **A thread cache (tcache)** takes most calls without any lock: 64 small bins
  plus 12 large bins in current glibc, up to 16 blocks per bin, with a
  per-process key in each cached block to catch double frees.
- **Up to 8 arenas per core.** `free()` finds a block's arena from its
  address: non-main heaps are aligned to their maximum size, so masking the
  pointer finds the heap header. It never searches.
- **A dynamic mmap threshold.** It starts at 128 KB and, each time a mapped
  block is freed, rises to that block's size, up to 32 MB. A program that
  keeps allocating big buffers stops paying for `mmap`/`munmap` each time.
- **Huge pages on request** with the `glibc.malloc.hugetlb` tunable
  (`MADV_HUGEPAGE`).

**The kernel.** Anonymous memory is faulted in large folios, 64 KB on arm64,
and the contiguous bit lets the TLB treat each folio as one entry. On a kernel
compile that saved about 5% of the time and 40% of the kernel time, plus 2%
more from the contiguous bit ([LWN](https://lwn.net/Articles/937239/)).
Redis's page faults fell from 262,100 to 18,550
([mTHP benchmarks](https://marc.info/?l=linux-mm&m=171939882613742)).

**Page size.** Linux runs 16 KB pages on Apple silicon (Asahi). Android 15
moved to 16 KB pages. Google measured app launch 3.16% faster on average (up
to 30%), 4.56% less power and boot 0.8 s faster, for about 9% more memory
([Android developers](https://android-developers.googleblog.com/2025/05/prepare-play-apps-for-devices-with-16kb-page-size.html)).

## What Darwin does

**libmalloc** ([source](https://github.com/apple-oss-distributions/libmalloc)):

- **Magazine malloc: one magazine per CPU** (up to 64). A thread uses the
  magazine of the CPU it is running on. Its lock is an `os_unfair_lock` taken
  with `OS_UNFAIR_LOCK_ADAPTIVE_SPIN`, so it spins before sleeping. `free()`
  goes to the magazine named in the block's region trailer (the region is
  found by masking the pointer), locks it, and re-checks the owner in case
  the region moved. A depot recirculates emptying regions between
  magazines.
- **Size tiers:**
  - tiny, up to 1008 B, 16 B quanta in 1 MB regions;
  - small, up to 32 KB, 512 B quanta in 8 MB regions;
  - medium, up to 8 MB, 32 KB quanta in 128 MB regions;
  - large, straight from the VM, with a cache of freed large blocks.
- **Thresholds scale with physical memory.** The medium tier switches on only
  at 32 GB of RAM. The large cache grows from 16 entries of up to 128 MB to
  64 entries of up to 512 MB at 32 GB. The resident part of that cache is
  held to `memsize >> 10`.
- **nano v2 for blocks up to 256 B.** Allocation and free are lock-free
  (compare-and-swap on the block's metadata), and each CPU has its own
  current block per size class. The blocks are 16 KB, Apple's page size, so
  an emptied one can be given back as a page.
- **xzone malloc, the newest,** is "originally based on mimalloc" and
  organised around the page. Its slices are 16 KB, "one operating system
  virtual page", in 4 MB segments. Blocks up to 4 KB come from 16 KB chunks,
  and blocks up to 32 KB from 64 KB chunks. Each zone starts with a single
  current chunk and moves to per-cluster or per-CPU chunks only when it
  measures contention. Thread caches start up the same way, on allocation
  volume or contention. Metadata is found through a segment table, and freed
  pages go into a ring buffer the kernel drains according to memory pressure.

## What we take from them, and the big picture

**Memory is plentiful now.** Haiku's allocators were tuned for machines with
a few hundred MB of memory and 32-bit address spaces; a Prose machine has
gigabytes and a 48-bit address space. Both Linux and Darwin trade memory for
speed where the cost is bounded, and Darwin scales its thresholds with RAM.
We should too: per-thread caches, and freed memory kept for reuse up to a
limit set by the machine's RAM, not by 2005's.

**Address space costs nothing.** A reserved range and an untouched page cost
no RAM. Haiku already commits *overcommitting* areas page by page as they are
touched (`B_OVERCOMMITTING_AREA`, what `mmap(MAP_NORESERVE)` asks for), and
its `MADV_FREE` gives back both the pages and their commitment. That is the
substrate glibc and libmalloc build on, and it makes area juggling
unnecessary. It is also how the mprotect problem that ruled out mimalloc is
avoided: nothing is ever protected page by page.

**The native page is 16 KB.** Apple silicon's own page is 16 KB. nano v2's
blocks and xzone's slices are 16 KB, and Linux and Android are moving to it.
Prose's kernel uses 4 KB pages today. Under the hypervisor the host maps the
guest's memory in 16 KB pages, but the TLB caches translations at the smaller
of the two, 4 KB. The allocator will work in 16 KB pages now, so a later 16 KB
kernel changes nothing in it. Parts of the arm64 port are already ready for
one: the boot loader selects the 16 KB granule when `B_PAGE_SIZE` is 16384,
the translation map takes the page size as a parameter, and our binaries'
segments are 64 KB-aligned (GNU ld's default for aarch64).

## The design: libroot

The OpenBSD core stays: the chunk bitmaps, the region tables and the page
caches all keep their behaviour. What changes is the path around them.

1. **Pages: one overcommitting heap area per 64 GB range.**
   - `PagesAllocator` reserves 64 GB of address space and makes it one
     overcommitting area. RAM is committed as pages are touched, never
     ahead.
   - Freed pages go back into its free trees and are never unmapped inside
     the heap, so no area is split or resized.
   - When the free memory still resident passes a limit, the largest free
     runs are discarded with `MADV_FREE`: the pages and their commitment go
     back, and the addresses stay ours. The limit is a quarter of the heap
     in use, at least 32 MB, at most RAM/64 (256 MB on a 16 GB machine).
   - Allocations over 1 GB still get their own area.
2. **16 KB allocator pages** (`MALLOC_PAGESHIFT` 14).
   - Objects up to 8 KB are carved from pages, in 32 size classes. LLVM's
     4 KB slabs become four to a page instead of a page each, with a
     region-table entry each.
   - Regions come in 16 KB units of address space. RAM is still used 4 KB
     at a time, by the kernel.
3. **A page owner map.** A two-level table over the 48-bit address space
   holds 2 bytes per 16 KB page: the owning pool, and the size class if the
   page holds small objects. `free()` learns a pointer's owner and size
   without taking a lock and without searching, as glibc's heap mask,
   Darwin's region trailer and xzone's segment table do. `findpool()`'s walk
   stays only as a fallback, for pointers the map does not know.
4. **Thread caches for objects up to 8 KB.** Each thread keeps, per size
   class, an array of pointers to blocks it freed: up to 64 blocks and at
   most 32 KB a class. A miss refills half a class under one lock, taking
   free blocks in bitmap order. A full class gives its older half back,
   locking each owning pool once per run. A second `free()` of the block
   last freed in its class is stopped at once; other double frees are
   caught when the cache gives the block back. A thread's cache is flushed
   when the thread exits.

   The classes are arrays, as jemalloc's thread caches are, rather than
   lists linked through the blocks, as glibc's are. So a freed block is not
   written to until it is handed out again. Haiku's allocators never wrote
   into freed blocks, and programs have come to depend on that without
   knowing it. With linked lists (0138), Slayer crashed at every refresh: it
   `dynamic_cast`s items it has just deleted, and the cast read the list
   link where the vtable pointer had been. Patch 0140 made the classes
   arrays. As a bonus, `free()` no longer touches the block's cache line,
   which doubled the throughput of frees from other threads.
5. **Pools and locks.**
   - Pools number twice the CPUs, rounded to a power of two, from 8 to 32.
   - A pool lock spins for about a microsecond before it sleeps in the
     kernel, as Darwin's adaptive spin does. With thread caches in front,
     the locks are taken for batches rather than for each call.

**Behaviour that changes on purpose:**

- **Heap memory is committed when first touched, as on Linux and macOS.** On
  a machine truly out of memory, the touch fails rather than `malloc`
  returning NULL. Thread stacks on Haiku already behave this way.
- **A process keeps more freed memory:** its threads' caches (typically a few
  KB each, at most about 1 MB), and free pages up to the limit above.

`MALLOC_OPTIONS` stays compiled out on Haiku, as upstream has it. The debug
heap (`libroot_debug`) is a separate allocator and is not touched.

## The design: kernel (follow-ups, separate decisions)

- **K1, fault-around for anonymous memory.** A fault in a heap area maps an
  aligned 64 KB group when memory is plentiful: Linux's mTHP idea without
  large folios. It cuts first-touch faults by up to 16 times.
- **K2, lazy `MADV_FREE`.** Pages marked free stay mapped until memory runs
  short, as Linux's `MADV_FREE` and Darwin's `MADV_FREE_REUSABLE` do, so the
  allocator's retention could track the system's actual memory pressure.
- **K3, a 16 KB kernel page.** The port has the hooks; the rest is an audit
  of `B_PAGE_SIZE` users, the drivers, the file cache and the application
  ABI. Android's figures suggest 5–10%.

## Tests and measurements

- `tools/malloc/malloctest`, the regression test. It covers:
  - every size class and region size, filled and verified;
  - calloc zeroing after reuse;
  - realloc across every boundary;
  - every alignment entry point;
  - `malloc_usable_size`;
  - cross-thread frees;
  - thread churn with blocks outliving their threads;
  - fork with allocating threads;
  - memory given back after a large free.

  It ends `SELFTEST PASS n/n`.
- `tools/malloc/mallocbench` measures throughput with 1–8 threads over four
  patterns: thread-local churn, producer/consumer frees, large buffers and
  realloc growth. It also reports areas and page faults.
- The Mojo compile, measured with `tools/bench/teamstat` and `profile`,
  before and after.

## Results

The libroot part is Haiku patch 0138. It was measured on Prose with 8 CPUs,
the new libroot loaded from the benchmark's own `lib/` directory, and the
old one from the image, on the same machine and boot.

`tools/malloc/mallocbench`, 4 million operations a thread (throughput of
all threads together; runs vary by up to about 2x on the VM):

| pattern | old, 1 thread | new, 1 thread | old, 4 threads | new, 4 threads | old, 8 threads | new, 8 threads |
|---|---|---|---|---|---|---|
| local churn | 20.0 M/s | 88–98 M/s | 16.3 M/s | 100–175 M/s | 0.25 M/s | 136–155 M/s |
| frees in other threads | 62 k/s | 16–21 M/s | 62 k/s (2 pairs) | 16–35 M/s | — | 20–26 M/s |
| realloc to 4 MB | 743/s | 25–37 k/s | 54/s | 22–31 k/s | 62/s | 3.5–6.4 k/s |

- With the old allocator, 8 threads of local churn took 130 s for 32
  million operations. 51 s of that was kernel time, sleeping and waking on
  the pool mutex two of them shared.
- Large buffers (64 KB–16 MB, every page touched) took 1.2 s for 1,000 in
  one thread before, with 674,000 faults. Now they take 38 ms for 4,000,
  with 8,500 faults, because freed memory is reused rather than unmapped.
  With 2 to 8 threads this pattern still costs 50,000–110,000 faults a run:
  two threads touching new pages of the one heap area wait for each other
  on its VM cache lock in the kernel's fault path (47% of that run's
  samples). K1 would cut the faults.

`mojo build stdlib_check.mojo`, cold cache:

| | old | new |
|---|---|---|
| 1 thread | 3.21–3.29 s, 198,000 faults, ~2,000 areas | 2.63–2.81 s, 68,000 faults, 37 areas |
| 8 threads | 5.11–5.27 s, 218,000 faults, ~2,800 areas | 3.47–3.76 s, 104,000 faults, 44 areas |
| peak resident, 8 threads | 250 MB | 325 MB |

- `malloc` and `free` fell from 36% of the 8-thread compile's samples
  (inclusive) to about 2%.
- Eight threads are still slower than one, and the reasons are no longer
  in the allocator. About 15% of the samples are userland mutex syscalls:
  - LLVM's `PassRegistry` and MLIR's `ParallelDiagnosticHandler` and
    `StorageUniquer`;
  - Haiku's `pthread_rwlock`, whose readers serialise on an internal mutex;
  - the kernel's user-mutex bookkeeping, a write-locked `rw_lock` taken on
    every contended lock and unlock.
- Other costs in that run:
  - the TLS descriptor resolver (patch 0132), about 4%;
  - `system_clock::now()`, polled by Mojo's own thread pool, 3.6%;
  - `arch_cpu_sync_icache()` on every page mapped or protected, executable
    or not, 1.9%.

Tests: `tools/malloc/malloctest` 10/10, and Haiku's own `calloc_test`,
`memalign_test`, `signal_in_allocator_test` and
`signal_in_allocator_test2`.

**What the tests found.** `signal_in_allocator_test2` hung on the first
build: 5 runs in 5.

- **Where it hung.** The test kills its second thread at once. That thread
  was still in malloc's one-time switch to multi-threaded pools, holding
  pool 1's lock.
- **Why the window had grown.** With 16 KB pages each pool has 513 buckets
  instead of 129, and the switch wrote NULL into every list head: about
  20 KB of fresh pages for each pool, faulted in while the lock was held.
- **The fix.** The pools are mapped zero-filled already, so the writes are
  skipped. Now 10 runs in 10 pass, on both allocators.
- **Still there.** The hazard itself is older than 0138: a thread killed
  during that switch leaves pool 1 locked, on the old allocator too. Doing
  the switch in the thread that creates the first thread would remove it.

**Still owed:**

- a desktop session on a full image with 0138: boot memory, apps. (The
  compiler on the full image, with 0138-0150, is done: 3.0 s with one
  thread and 3.7 s with eight, and the standard library's suite passes
  193, fails the 2 that need Python, in 39% less time than before --
  MojoProse's PORT-JOURNAL, "The suite again".)
- the kernel follow-ups K1–K3;
- Haiku's `pthread_mutex` and `pthread_rwlock`, which do not spin, and whose
  rwlock readers serialise;
- per-pool page ranges, if the fault path's cache lock matters to a real
  program.
