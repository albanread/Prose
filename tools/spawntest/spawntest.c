/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	Regression test for Haiku patches 0133 and 0134: posix_spawn() runs the
	path it is given, and passes argv[0] through as it is, whatever argv[0]
	says; and a spawn that cannot exec its program does not write the
	parent's buffered output a second time.

	Run from the directory that holds spawnhelper.
*/

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


static int sPassed = 0;
static int sFailed = 0;


static void
check(int ok, const char* name, const char* detail)
{
	if (ok) {
		sPassed++;
		printf("PASS %s\n", name);
	} else {
		sFailed++;
		printf("FAIL %s: %s\n", name, detail);
	}
}


/*!	Spawns path with argv {argv0}, telling the helper to expect expectArgv0,
	and returns its exit code, or -1 and the error in *_error.
*/
static int
spawn_helper(bool usePath, const char* path, const char* argv0,
	const char* expectArgv0, int* _error)
{
	char expect[256];
	snprintf(expect, sizeof(expect), "SPAWN_EXPECT_ARGV0=%s", expectArgv0);
	const char* pathValue = getenv("PATH");
	char pathVariable[1300];
	snprintf(pathVariable, sizeof(pathVariable), "PATH=%s",
		pathValue != NULL ? pathValue : "");
	char* const envp[] = { expect, pathVariable, NULL };
	char* const argv[] = { (char*)argv0, NULL };

	pid_t pid;
	int error = usePath
		? posix_spawnp(&pid, path, NULL, NULL, argv, envp)
		: posix_spawn(&pid, path, NULL, NULL, argv, envp);
	*_error = error;
	if (error != 0)
		return -1;

	int status;
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}


static void
check_spawn(const char* name, bool usePath, const char* path,
	const char* argv0)
{
	int error;
	int code = spawn_helper(usePath, path, argv0, argv0, &error);
	char detail[256];
	if (code == -1)
		snprintf(detail, sizeof(detail), "spawn failed: %s", strerror(error));
	else
		snprintf(detail, sizeof(detail), "the helper exited with %d", code);
	check(code == 7, name, detail);
}


int
main()
{
	char cwd[1024];
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return 1;
	char helper[1100];
	snprintf(helper, sizeof(helper), "%s/spawnhelper", cwd);
	if (access(helper, X_OK) != 0) {
		printf("run from the directory that holds spawnhelper\n");
		return 1;
	}

	// What posix_spawn() users do: the path, and just the name in argv[0].
	// The kernel used to load argv[0] instead: ENOENT.
	check_spawn("absolute path, the name in argv[0]", false, helper,
		"spawnhelper");
	check_spawn("absolute path, another argv[0]", false, helper,
		"some other name");
	check_spawn("absolute path, the path in argv[0]", false, helper, helper);
	check_spawn("posix_spawnp, absolute path, the name in argv[0]", true,
		helper, "spawnhelper");

	// posix_spawnp() searches PATH for a name. (So does Haiku's exec for a
	// path without a slash, where POSIX takes it to be relative to the
	// working directory; Haiku's own programs rely on that, and it is kept.)
	char path[1200];
	snprintf(path, sizeof(path), "/bin:%s", cwd);
	setenv("PATH", path, 1);
	check_spawn("posix_spawnp, a name on PATH", true, "spawnhelper",
		"spawnhelper");

	// A missing program is an error, not a child.
	int error;
	int code = spawn_helper(false, "/no/such/program", "program", "program",
		&error);
	check(code == -1 && error == ENOENT, "a missing program is ENOENT",
		strerror(error));

	// A failed spawn's child must not flush its copy of the parent's
	// buffers. A process whose standard output is a file prints a line,
	// leaves it buffered, and spawns a program that does not exist: the
	// file must hold the line once.
	char scratch[1100];
	snprintf(scratch, sizeof(scratch), "%s/spawntest.out", cwd);
	fflush(stdout);
	pid_t tester = fork();
	if (tester == 0) {
		int fd = open(scratch, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0)
			_exit(1);
		close(fd);
		printf("pending\n");
		spawn_helper(false, "/no/such/program", "program", "program", &error);
		exit(0);
	}
	int status = 0;
	waitpid(tester, &status, 0);
	int lines = 0;
	char line[64];
	FILE* file = fopen(scratch, "r");
	while (file != NULL && fgets(line, sizeof(line), file) != NULL)
		lines++;
	if (file != NULL)
		fclose(file);
	unlink(scratch);
	char detail[64];
	snprintf(detail, sizeof(detail), "the pending line was written %d times",
		lines);
	check(lines == 1, "a failed spawn leaves the parent's output alone",
		detail);

	printf("SELFTEST %s %d/%d\n", sFailed == 0 ? "PASS" : "FAIL", sPassed,
		sPassed + sFailed);
	return sFailed == 0 ? 0 : 1;
}
