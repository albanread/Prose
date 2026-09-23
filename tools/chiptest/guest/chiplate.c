// Regression test for Haiku patch 0127: a chip command that timed out must not
// have its late answer taken for the next command's.
//
// Start the machine with "--chip-late-reply 7", so that the host answers the
// first command seven seconds late -- after the driver has given up on it at
// five -- then in the guest:
//
//	clang -o chiplate chiplate.c && ./chiplate
//
// Command 1 times out; command 2 waits for command 1's late answer, collects
// it, and only then sends its own, so it takes the remaining ~2 s and gets its
// own answer; command 3 is immediate. Before the fix, command 2 came back at
// once holding command 1's answer, and every command after read its
// predecessor's.
#include <Drivers.h>
#include <OS.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main()
{
	int device = open("/dev/misc/prose/chip/0", O_RDWR);
	if (device < 0) {
		printf("FAIL: open: %s\n", strerror(errno));
		return 1;
	}

	int failures = 0;
	for (int i = 1; i <= 3; i++) {
		uint32 state = 0xdeadbeef;
		bigtime_t start = system_time();
		int result = ioctl(device, B_DEVICE_OP_CODES_END + 5, &state, sizeof(state));
		status_t status = result < 0 ? errno : B_OK;
		long long ms = (system_time() - start) / 1000;
		printf("command %d: %s after %lld ms, state %#x\n", i, strerror(status), ms,
			(unsigned)state);

		bool expected = i == 1 ? status == B_TIMED_OUT : status == B_OK;
		printf("  %s\n", expected ? "PASS" : "FAIL");
		if (!expected)
			failures++;
	}
	close(device);
	printf(failures == 0 ? "SELFTEST PASS 3/3\n" : "SELFTEST FAIL\n");
	return failures;
}
