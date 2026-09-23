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

Not measured: low contention, where one LSE instruction against a call and a
loop may well win.
