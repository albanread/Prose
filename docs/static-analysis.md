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

The first five were already on the Mac; cppcheck, flawfinder and semgrep
were installed from Homebrew afterwards, when asked for.

| tool | from | what it looks for |
|---|---|---|
| clang 22 warnings (`-Wdangling`, `-Wuninitialized`, `-Wsizeof-pointer-memaccess`, tautologies, `-Winfinite-recursion`, ...) | Homebrew LLVM | local mistakes the compiler can prove |
| clang static analyzer, through clang-tidy (`clang-analyzer-*` and ~45 `bugprone-*`/`misc-*` checks) | Homebrew LLVM | path-sensitive: null dereference, use after free, leaks, out of bounds, uninitialised values, division by zero |
| clang-query with AST matchers of our own | Homebrew LLVM | a pointer kept, assigned or returned from a temporary that is destroyed at the end of the statement; `ssize_t` results compared with `B_OK` |
| ast-grep 0.42.1 with rules of our own (`tools/static-analysis/rules`) | the NewReview tool cache | Haiku API traps: I/O byte counts compared with `B_OK`, `Find()`/`IFindLast()` compared with NULL, unclamped `snprintf` accumulation, user pointers dereferenced in the kernel |
| GCC 13 `-fanalyzer` | the Prose cross compiler | C files of the kernel and libroot, with the real build flags |
| cppcheck 2.21 | Homebrew | its own flow analysis: leaks, out of bounds, uninitialised values; told the target's architecture macros, or it stops at `HaikuConfig.h` |
| flawfinder 2.0.20 | Homebrew | lexical: `strcpy`, `sprintf`, `system()`, check-then-use races (levels 4 and 5) |
| semgrep 1.176 | Homebrew | nothing useful without an account: the registry's C and C++ rule packs need `semgrep login` (`p/c` has two rules, `p/cpp` is a 404); run with `--metrics=off` |

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

clang-tidy's analyzer and bugprone checks reported 474 findings in the
core: 91 in the kernel, 30 in libroot and the runtime loader, 301 in the
kits, 52 in the servers and applications. Each was read against the code
and its callers. 80 are real (17%), 393 are false positives, and one
stayed unclear. The 80 are 52 distinct bugs. Reading the code around them
found about 25 more, cppcheck 3, a clang warning 1, and our own matchers
the two that started this (0139) and Mail's. The regression tests found two
more (0149, 0150). Every one is in upstream Haiku code; nothing real turned
up in Prose's own patches (the new allocator's two findings are false
positives).

The worst of them:

- **One line in a settings file made the system unbootable**:
  `BlockedEntries { "" }` in `/boot/system/settings/packages` made the boot
  loader write through an unset pointer (0150), and packagefs in the kernel
  too (0142).
- **Any process could panic the kernel**: `writev()` with an empty vector
  on a FAT, NTFS or NFSv4 file branched to address 0 in the file cache
  (0141; FAT then also called a write of nothing an I/O error, 0149).
- **Any application could take the registrar down**: `GetRecentDocuments()`
  copied its file type into a 240 byte buffer without a bound (0148).
- **app_server crashed** drawing a copy of a picture that draws another
  one, and **hung** with a bold system font above about 300 points (0147).
- **Tracker deleted a file** when a folder of the same name was moved onto
  it, after the wrong prompt: a typo had turned off the "cannot replace a
  file with a folder" check (0146).
- **libroot**: `syslog()` of a long message wrote past its stack buffer;
  a detached main thread or a timer thread that exited freed memory that
  was never allocated; a signal handler ending in `mmap()` crashed the
  runtime loader; `parsedate()` got every date with a dash wrong (0143).
- **Hangs**: `BQuery` on a predicate with a lone `%`, and so Tracker's
  Find with a quote and a percent sign (0145).
- **Leaks**: a file descriptor for every file `BMediaFile` cannot read;
  Deskbar on every menu open and application launch (0145, 0148).

| patch | bugs fixed |
|---|---|
| 0141 kernel | 9 |
| 0142 packagefs | 2 |
| 0143 libroot, runtime_loader | 7 |
| 0144 app, interface, support kits | 14 |
| 0145 storage, translation, network, media kits | 13 |
| 0146 Tracker | 9 |
| 0147 app_server | 6 |
| 0148 servers and applications | 10 |
| 0149 fat, ntfs, nfs4 (found by the tests) | 1 |
| 0150 boot loader (found by the tests) | 1 |

`patches/haiku/README.md` lists them per patch, and each commit message
says what went wrong.

### Why most findings were false

- `debugger()` and `ThrowOnAssert()` stop the code, but are not declared
  noreturn (the latter is out of line), so the analyzer walks past them.
- A failing `socket()` or `open()` is assumed to leave `errno` 0.
- Invariants the analyzer loses: list heads, counts that bound indices,
  fields set in one method and used in another, values another thread
  writes before it releases a semaphore.
- Paths that need a small allocation to fail, and inputs no caller passes.

### Tests

`tools/analysis-fixes`: 34 tests in `afxtest`, each in a child process of
its own, nine of them also under the guarded heap, and a boot test around
a reboot. Before is the image with 0138-0140, after the one with 0141-0150;
both booted headless on VZ.

| test | before | after |
|---|---|---|
| writev-zero-length (FAT) | kernel panic: `vm_page_fault: unhandled page fault in kernel space at 0x0, ip 0x0`, from `cache_io` in `dosfs_write` | PASS |
| packagefs boot test | the next boot stops in the boot loader (VZ: "Internal Virtualization error") | PASS |
| kmessage-set-twice | crash | PASS |
| syslog-long-message | crash | PASS |
| pthread-detach-main, timer-thread-exit | crash | PASS |
| commpage-image-lookup | hang | PASS |
| parsedate-dash | "12-25-2020 10:30" is 00:10 | PASS |
| driver-settings-empty-assignment | `key` missing | PASS |
| utimensat-now | the time set to 0 | PASS |
| application-quit-unlocked, password-key-copy | crash | PASS |
| channel-count-grow | values 137691143 and 55 | PASS |
| font-zero-size-width | 1.9e-43 | PASS |
| shape-copy-empty | PASS (no visible effect) | PASS |
| string-escape-in-place | garbage; crash under the guarded heap | PASS |
| url-long-scheme | not valid | PASS |
| column-drag-past-last | the column stays first | PASS |
| query-lone-percent | hang | PASS |
| translator-path-list | no translators | PASS |
| text-sniffer | PASS (the over-read is not deterministic) | PASS |
| net-endpoint-closed | crash | PASS |
| ipv6-prefix-length | /60 is 56 | PASS |
| media-file-descriptors | 41 descriptors leaked by 40 files | PASS |
| media-encoder-bad-format, media-theme-empty-web | crash | PASS |
| adapter-io-flush | 15.7 MB leaked | PASS |
| tracker-glob | "ab" matches "???"; crash under the guarded heap | PASS |
| nav-menu-hidden-link | crash under the guarded heap | PASS |
| rating-edit-without-view | crash | PASS |
| gradient-without-stops | stack contents painted | PASS |
| bitmap-length-overflow | a bitmap of 4 rows of 1 GB | PASS |
| registrar-long-type | the registrar stops answering | PASS |
| picture-clone-nested | app_server stops answering | PASS |
| decorator-wide-border | app_server stops answering | PASS |

After: `SELFTEST PASS 32/32` (the FAT test runs on its own), 9/9 under the
guarded heap, 1/1 for the decorator and 1/1 on FAT, and the boot test.

### Not fixed

- **NetworkDevice**: a 32-byte SSID is cut to 31 characters. The public
  `wireless_network::name[32]` cannot hold it; changing it breaks the ABI.
- **DebugLooper**: if `create_sem()` fails, a caller can return while its
  stack `Job` is still queued. It needs semaphore exhaustion.
- **MessageAdapter**: the R5 flattener (dead code) and the Dano
  unflattener read sizes as `ssize_t`, 8 bytes on 64-bit, where the formats
  have 4; the Dano path was not verified.
- Latent, no caller reaches them: `BDate` of a `time_t` that `gmtime_r()`
  rejects; `Err::operator=` on itself; `DriverSettingsMessageAdapter` with an
  unnamed `parent_value`; the KDL `mount` command while a mount is in
  progress; the kernel's `strtod()` without memory.
- Hygiene: a zero-length VLA in `BBufferConsumer`, three redundant `.End()`
  calls on layout builders, static state in `radixsort()`, `wrterror()` not
  marked noreturn in malloc.c.
- app_server takes any system font size; with 0147 a 400 point bold font is
  drawn instead of hanging, so there is no limit to add for safety.
- Paths no test reaches (they need an allocation to fail, KDL, a crafted
  add-on, the remote desktop protocol, or a mouse) were checked by reading
  the code: `tools/analysis-fixes/README.md` lists them.
- Semgrep's registry rules for C and C++ need an account (`semgrep
  login`); without one it had nothing to run.

