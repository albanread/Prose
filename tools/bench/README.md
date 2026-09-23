# Kernel contention benchmark

`kbench.c` measures the kernel, not itself: every operation is a syscall that
takes kernel spinlocks and does kernel atomics. Build and run it in the guest:

	clang -O2 -o kbench kbench.c && ./kbench 14

Two loads, three rounds of 3 s each, across N threads:

- **create** — `create_sem` + `delete_sem`, contending on the global semaphore table
- **shared** — `acquire_sem` + `release_sem` on one semaphore, one hot lock

A program built *in* the guest uses the guest compiler's own flags, so a
userland atomics loop would say nothing about how the tree was built; these
syscalls do.

## What it decided (2026-09-23)

Whether Prose should build for ARMv8.4-A so the kernel's atomics are LSE
instructions. Every build below was from clean (jam does not rebuild on a
flags change), 14 threads, on an M4 Max under Virtualization.framework:

| build | boots | create/delete |
|---|---|---|
| ARMv8.0 — LL/SC through GCC's outline helpers (**shipped**) | yes | 575–701k ops/s |
| ARMv8.4-A — inline LSE | yes | **385–388k ops/s** |
| ARMv8.4-A `+nolse` — LL/SC through the helpers | yes | 732–751k ops/s |
| ARMv8.4-A `+nolse -mno-outline-atomics` — inline LL/SC | **no** | — |

LSE was about 45% *slower* on the contended kernel path, and not because of
anything else ARMv8.4-A changes: the `+nolse` build keeps the rest and is as
fast as ARMv8.0 or faster. So Prose stays on ARMv8.0. The shared-semaphore
load was too noisy (130–165k) to separate the builds.

The inline LL/SC build did not boot: a userland mutex found its lock bit
already clear on unlock (`mutex was not actually locked!`,
`src/system/libroot/os/locks/mutex.cpp`), in that build and in none of 83
others. That points at a latent lost update which the outline helpers happen
to hide. It is not understood yet.

## Why LSE lost -- what is known (2026-09-23, second pass)

Measured again with the thread spread reported, and single-threaded:

| | LL/SC (ARMv8.0) | LSE (ARMv8.4-A) |
|---|---|---|
| 1 thread, create/delete | 1.94M/s | **2.07M/s** |
| 1 thread, shared | 2.67M/s | **2.75M/s** |
| 14 threads, create/delete | **660k/s**, spread 2.0-2.3x | 400k/s, spread 2.1-2.4x |
| 14 threads, shared | ~145k/s, spread 1.0x | ~151k/s, spread 1.0x |

Two explanations are ruled out. The instructions are not slow: uncontended,
LSE is faster, as its shorter path should be. And LL/SC is not winning by
starving threads: the busiest thread does about twice the idlest's work under
both, so the spread is the same.

What fits every number, unproven: a thundering herd on the global semaphore
spinlock. At KDEBUG_LEVEL 2, DEBUG_SPINLOCKS is on, so releasing a spinlock is
a swap, not a store. When the holder releases, every spinning CPU sees the lock
free at once and swaps in a 1 -- one wins, the rest write for nothing, each
needing the line exclusively -- and the next holder's release swap waits behind
them. Inline LSE lets those swaps land within nanoseconds of each other; the
outline helper's call put a little accidental backoff in front of each. The
shared load shows no difference because a contended semaphore makes threads
sleep in a FIFO queue rather than spin: no herd, and a spread of exactly 1.0x.

It predicts two things, neither yet tried: backoff in acquire_spinlock() after
a failed swap should let LSE beat LL/SC under contention; and a kernel without
DEBUG_SPINLOCKS, whose release is a plain store, should narrow the gap. If both
hold, the fault is the spinlock's stampede, not the choice of instruction --
and a spinlock that does not stampede would help either.

# Blitting benchmark

`blitbench.c` measures the instructions that move pixels. The Prose accelerant
has no 2D hooks, so app_server does every copy and fill itself, on the CPU:

- the back buffer goes to the front one row at a time through `memcpy`
  (`HWInterface::_CopyToFront`)
- scrolling is `memcpy`/`memmove` per row (`DrawingEngine::_CopyRect`), and so
  is drawing a 32-bit bitmap with `B_OP_COPY` (`DrawBitmapNoScale.h`)
- rectangles are filled by `gfxset32` (`drawing_support.h`)

On arm64 those `memcpy`, `memmove` and `memset` are libroot's portable C
(`generic_memcpy.c`, `generic_memset.c`, musl's `memmove.c`); x86_64 has tuned
ones, arm64 none. As GCC builds them, they move one 8-byte register per
iteration -- and when source and destination differ in alignment modulo 8,
one *byte* per iteration, for the whole copy. `gfxset32` stores one 8-byte
register per iteration. Nothing in the pixel path uses the vector registers
or `dc zva`.

It builds on macOS too, where the same loops give a reference for the same
core. In the guest (boot one machine with `scripts/run-machine.sh`):

	clang -O2 -o blitbench blitbench.c && ./blitbench && ./blitbench 256

The source does not fit in one portal request; send it in pieces.

## What it found (2026-09-23)

M4 Max, best of five trials after warm-up. Guest: Prose under
Virtualization.framework, 4 CPUs. Host: the same loops on macOS, where
`memcpy`, `memmove` and `memset` are Apple's. The frame is 1920x1080x32
(7.9 MB), which fits in L2; 256 MB is past every cache. GB/s:

| | guest, frame | guest, 256 MB | host, frame |
|---|---|---|---|
| memcpy, aligned | 35.3 | 32.2 | 88.6 |
| memcpy, off by one pixel | **4.5** | **4.1** | 86.3 |
| memmove, scroll one pixel | **4.5** | **4.4** | 69.1 |
| neon copy, aligned | 66.2 | 61.2 | 67.2 |
| neon copy, off by one pixel | 65.8 | 59.4 | 67.1 |
| neon move, scroll one pixel | 56.3 | 55.3 | 60.8 |
| memset | 35.7 | 35.4 | 140.8 |
| gfxset32, app_server's fill | 35.8 | 35.4 | 35.8 |
| neon fill | 133.0 | 133.6 | 139.9 |
| memset, zero | 35.9 | 35.6 | 282.1 |
| dc zva, zero | 282.7 | 213.6 | 281.5 |

- **The byte loop is the worst of it.** A sideways scroll by an odd number
  of pixels, a 32-bit bitmap copied to an odd x, and every other row of a
  bitmap with an odd width (rows are padded only to 4 bytes) all run at
  4.5 GB/s -- fifteen times slower than a vector loop doing the same
  misaligned copy. The fallback is for processors that fault on unaligned
  access. arm64 does not, on Normal memory, and on these cores misalignment
  costs next to nothing (65.8 against 66.2).
- Aligned copies -- every back-to-front copy, vertical scrolls -- reach
  35 GB/s, 40% of what macOS's `memcpy` does on the same core.
- Fills reach a quarter of a vector fill. Zeroing reaches an eighth of
  `dc zva`, which Prose's kernel allows at EL0 (DCZID_EL0.DZP clear, 64-byte
  blocks).
- The virtual machine is not the limit: the guest's vector loops run as fast
  as the host's.
- The memory type is right. prose_display maps the pool
  `B_WRITE_BACK_MEMORY` and clones keep the type, so the frame and back
  buffers are Normal memory, where unaligned, vector and `dc zva` accesses
  are all legal. On Device memory they would fault.
- FEAT_MOPS, the architecture's own copy and set instructions, is no way
  out: the M4 Max does not report it, and M1s never had it.

At 1080p, a full-screen back-to-front copy costs 0.24 ms today and a
full-screen sideways scroll by one pixel 1.9 ms, about a ninth of a 60 Hz
frame. Both scale with the pixel count.

What would fix it, not yet done: arm64 `memcpy`, `memmove` and `memset` in
libroot from Arm's optimized-routines (`string/aarch64/memcpy-advsimd.S` and
`memset.S`, MIT OR Apache-2.0 WITH LLVM-exception) -- q-register pairs,
unaligned heads and tails, `dc zva` for large zero fills, and one entry point
for `memcpy` and `memmove` that handles overlap. That last matters here:
`DrawingEngine::_CopyRect` calls `memcpy` on overlapping rows when it moves
content left, and that works today only because the portable C copies
forwards. And `gfxset32` as a vector loop: a 32-bit colour is not a byte, so
`memset` cannot do its job.
