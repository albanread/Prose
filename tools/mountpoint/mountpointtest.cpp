/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	mountpointtest: the regression test for Haiku patch 0139.

	BPartition::GetMountPoint() of an unmounted partition names the mount
	point after the volume, "/<volume name>" -- which is how mount_server
	mounts the host's shared folders at /HostFS. It kept a pointer into the
	temporary BString ContentName() returns, and read the name after the
	string was freed. The allocator of patch 0138 reuses a freed block's
	first words, so the name came out as garbage: HostFS was mounted at a
	directory named with pointer bytes, and for the EFI partition the call
	never returned.

	For every partition with a file system, GetMountPoint() must answer
	within five seconds; for an unmounted one, with "/" and the volume's
	name ('/' replaced by '-'), and perhaps a number.
*/

#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <Partition.h>
#include <Path.h>
#include <String.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static int sPassed = 0;
static int sTotal = 0;


static void
alarm_handler(int)
{
	printf("FAIL GetMountPoint() did not return within five seconds\n");
	printf("SELFTEST FAIL %d/%d\n", sPassed, sTotal + 1);
	_exit(1);
}


class Checker : public BDiskDeviceVisitor {
public:
	virtual bool Visit(BDiskDevice* device)
	{
		return Visit(device, 0);
	}

	virtual bool Visit(BPartition* partition, int32 /*level*/)
	{
		if (!partition->ContainsFileSystem())
			return false;

		BPath path;
		partition->GetPath(&path);
		BString name = partition->ContentName();

		alarm(5);
		BPath mountPoint;
		status_t status = partition->GetMountPoint(&mountPoint);
		alarm(0);

		bool ok = status == B_OK;
		BString expected;
		if (ok && !partition->IsMounted()) {
			// "/<name>", and maybe a number to make it unique
			expected = name;
			expected.ReplaceAll('/', '-');
			expected.Prepend("/");
			ok = strncmp(mountPoint.Path(), expected.String(),
				expected.Length()) == 0;
			for (const char* rest = mountPoint.Path() + expected.Length();
					ok && *rest != '\0'; rest++) {
				ok = *rest >= '0' && *rest <= '9';
			}
		}

		sTotal++;
		if (ok)
			sPassed++;
		printf("%s %s (\"%s\", %s): mount point %s%s%s\n", ok ? "PASS" : "FAIL",
			path.Path(), name.String(),
			partition->IsMounted() ? "mounted" : "not mounted",
			status == B_OK ? mountPoint.Path() : strerror(status),
			expected.Length() > 0 ? ", expected " : "", expected.String());
		return false;
	}
};


int
main()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGALRM, alarm_handler);

	BDiskDeviceRoster roster;
	Checker checker;
	roster.VisitEachPartition(&checker);

	if (sTotal == 0)
		printf("(no partition with a file system to check)\n");
	printf("SELFTEST %s %d/%d\n", sPassed == sTotal && sTotal > 0
		? "PASS" : "FAIL", sPassed, sTotal);
	return sPassed == sTotal && sTotal > 0 ? 0 : 1;
}
