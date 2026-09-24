# Regression tests for the static-analysis fixes

The tests for Haiku patches 0141-0150, the bugs that the static analysis
of September 2026 found (`docs/static-analysis.md`, `tools/static-analysis`).
One test per bug that a program can reach; each names the file it tests and
what went wrong there.

| file | what |
|---|---|
| `afxtest.cpp` | the tests: each in a child process of its own, so a crash or a hang fails that test only |
| `build.sh` | cross-compiles `afxtest` on the Mac (the Prose tree's cross g++, ProseWriter's sysroot, Tracker's headers from the tree) |
| `run-guest.sh` | on Prose: runs everything the way it needs to run (below) |
| `packagefs-boot-test.sh` | on Prose: the boot test for 0142 and 0150, in two halves around a reboot |

## Running

On the Mac:

	tools/analysis-fixes/build.sh <shared folder>/afx
	cp tools/analysis-fixes/*.sh <shared folder>/afx/

On Prose:

	sh /HostFS/afx/run-guest.sh /HostFS/afx after

`run-guest.sh` sets debug_server's `default_action kill`, so a crashing
test is killed at once instead of waiting in a crash alert that nobody
answers on a headless machine. It then runs:

1. every test but the FAT one;
2. the tests whose bug shows only under the guarded heap, again under it
   (`LD_PRELOAD=libroot_debug.so MALLOC_DEBUG=g`): reading freed memory or
   past a block then faults, where the normal allocator returns stale bytes;
3. the intrusive test (default decorator and a 400 point bold font, both
   restored afterwards), unless `--gentle`;
4. `writev-zero-length` on a FAT disk image made with `diskimage`, `mkfs`
   and `mount` -- last, because a kernel without 0141 panics (and FAT
   without 0149 calls a write of nothing an I/O error).

The results file is copied to the directory given after every step, so a
panic keeps what came before it. Each test prints `PASS`, `FAIL` with the
reason, or `SKIP`; each run ends with `SELFTEST PASS n/n`.

The packagefs test needs a reboot:

	sh /HostFS/afx/packagefs-boot-test.sh block    # then reboot
	sh /HostFS/afx/packagefs-boot-test.sh check

`block` puts `BlockedEntries { "" bin/fortune }` into the system's packages
settings; after the reboot, `check` expects the system up and bin/fortune
gone, and restores the settings. Without 0150 there is no second half: the
boot loader stops the machine (on VZ, "Internal Virtualization error").

## What the tests cannot reach

Out-of-memory paths (`Stack::Push()`, `BMessage`, `BLayoutBuilder`, the
layouters, `vfs_new_io_context()`, the module iterator), KDL commands
(`file_map_stats`), a crafted kernel add-on (`load_kernel_add_on()`), the
remote desktop protocol, and paths that need a mouse or a person: Tracker's
replace-a-file-with-a-folder alert, its view state and trash lists, Deskbar's
menus, Mail's reply account, `BTextView`'s context menu. These were verified
by reading the code; mouse events do not reach windows on the dev VM.
