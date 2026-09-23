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
