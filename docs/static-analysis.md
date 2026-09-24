# Static analysis of the Prose tree

*2026-09-24. The kernel, libroot, the runtime loader, the core kits, the
servers, Tracker and Deskbar, and our drivers, scanned with the analyzers
we have, after patch 0138's allocator exposed a use-after-free that the old
allocator had hidden for years.*

## Why

Patch 0138's thread caches link freed blocks through their first words.
The old allocator left freed bytes alone, so code that read memory after
freeing it kept seeing what had been there. With the new allocator, mount_server mounted the host's shared folders
at a directory named with pointer bytes, and `BPartition::GetMountPoint()`
never returned for the EFI partition. The cause was a pointer kept into a
temporary `BString` (patch 0139). If one such mistake was there, there
would be others, and a scan finds them faster than a desktop session does.

## Tools, and how they were run

Nothing was installed. Every tool below was already on the Mac.

| tool | from | what it looks for |
|---|---|---|
| clang 22 warnings (`-Wdangling`, `-Wuninitialized`, `-Wsizeof-pointer-memaccess`, tautologies, `-Winfinite-recursion`, ...) | Homebrew LLVM | local mistakes the compiler can prove |
| clang static analyzer, through clang-tidy (`clang-analyzer-*` and ~45 `bugprone-*`/`misc-*` checks) | Homebrew LLVM | path-sensitive: null dereference, use after free, leaks, out of bounds, uninitialised values, division by zero |
| clang-query with AST matchers of our own | Homebrew LLVM | a pointer kept, assigned or returned from a temporary that is destroyed at the end of the statement; `ssize_t` results compared with `B_OK` |
| ast-grep 0.42.1 with rules of our own (`tools/static-analysis/rules`) | the NewReview tool cache | Haiku API traps: I/O byte counts compared with `B_OK`, `Find()`/`IFindLast()` compared with NULL, unclamped `snprintf` accumulation, user pointers dereferenced in the kernel |
| GCC 13 `-fanalyzer` | the Prose cross compiler | C files of the kernel and libroot, with the real build flags |

- **The compile database** comes from jam's dry run: `jam -n -a -dx
  <targets>` prints every command and builds nothing.
  `tools/static-analysis/mkcdb.py` turns the cross gcc commands into clang
  commands for `--target=aarch64-unknown-haiku`, with clang's builtin
  headers in place of gcc's.
- **Coverage.** 2,161 source files for the core targets, and 5,384 for the
  whole `@prose-mmc` image. Clang parses all but two: two C files assign
  between incompatible pointer types, which gcc tolerates.
- **Run times on the Mac, 16 cores:**
  - the clang warnings: 12 s;
  - the matchers: 6 s for the core, 30 s for the whole image;
  - ast-grep: 1.4 s;
  - GCC's analyzer: 2 s;
  - clang-tidy with the full analyzer: about 2 minutes for the core.

## Findings

*(verdicts in progress; the table below is filled in as triage lands)*
