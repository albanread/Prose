# GetMountPoint test

The regression test for Haiku patch 0139. `BPartition::GetMountPoint()`
names the mount point of an unmounted partition after its volume, as
`/<volume name>`. It kept a pointer into the temporary `BString` that
`ContentName()` returns, and read the name after that string was freed.

For years the old allocator left freed bytes as they were, so the mistake
was invisible. The allocator of patch 0138 writes its free list into a
freed block's first words, and then:

- mount_server mounted the host's shared folders at a directory named with
  pointer bytes instead of `/HostFS`;
- `GetMountPoint()` of the unmounted EFI partition never returned.

DriveSetup's disk view had the same mistake.

On Prose:

	g++ -O2 -o mountpointtest mountpointtest.cpp -lbe
	./mountpointtest

Every partition with a file system is checked. `GetMountPoint()` must
answer within five seconds; for an unmounted partition it must answer
`/<volume name>`, perhaps with a number. `SELFTEST PASS n/n` with 0139.
The EFI partition, "prose boot", is the unmounted one on a Prose disk.
